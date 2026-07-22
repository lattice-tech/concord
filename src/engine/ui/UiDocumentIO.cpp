#include "engine/ui/UiDocumentIO.h"

#include "engine/debug/Logger.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Concord::UI {

namespace {

// 'C','U','I','1' as a little-endian tag, plus a version guard.
constexpr std::uint32_t kMagic = 0x31495543u;
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kHeaderBytes = sizeof(std::uint32_t) * 3u;
constexpr std::size_t kWidgetFixedBytes = 35u;

bool CheckedAdd(std::size_t a, std::size_t b, std::size_t& out) noexcept
{
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

bool CheckedMultiply(std::size_t a, std::size_t b, std::size_t& out) noexcept
{
    if (a != 0u && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

bool IsValid(WidgetKind kind) noexcept
{
    return kind == WidgetKind::Panel || kind == WidgetKind::Label
        || kind == WidgetKind::Button;
}

bool IsValid(Align align) noexcept
{
    return align == Align::Start || align == Align::Center || align == Align::End;
}

bool ValidateWidget(const Widget& widget,
                    std::unordered_set<std::uint32_t>& buttonIds)
{
    if (!IsValid(widget.kind) || !IsValid(widget.hAlign) || !IsValid(widget.vAlign)
        || !std::isfinite(widget.rect.x) || !std::isfinite(widget.rect.y)
        || !std::isfinite(widget.rect.width) || !std::isfinite(widget.rect.height)
        || !std::isfinite(widget.fontScale) || widget.fontScale <= 0.0f
        || widget.rect.width <= 0.0f
        || widget.text.size() > UiDocumentIO::kMaxWidgetTextBytes) {
        return false;
    }

    // A zero-height label is an established intrinsic single-line shorthand.
    // Panels and buttons must have a positive interactive/painted area.
    if ((widget.kind == WidgetKind::Label && widget.rect.height < 0.0f)
        || (widget.kind != WidgetKind::Label && widget.rect.height <= 0.0f)) {
        return false;
    }
    if (widget.kind == WidgetKind::Button) {
        return widget.id != 0u && buttonIds.insert(widget.id).second;
    }
    return widget.id == 0u;
}

bool ValidateDocument(const UiDocument& document, std::size_t& encodedBytes)
{
    if (document.widgets.size() > UiDocumentIO::kMaxWidgetCount) {
        return false;
    }

    std::size_t fixedBytes = 0;
    if (!CheckedMultiply(document.widgets.size(), kWidgetFixedBytes, fixedBytes)
        || !CheckedAdd(kHeaderBytes, fixedBytes, encodedBytes)) {
        return false;
    }

    std::unordered_set<std::uint32_t> buttonIds;
    buttonIds.reserve(document.widgets.size());
    for (const Widget& widget : document.widgets) {
        if (!ValidateWidget(widget, buttonIds)
            || !CheckedAdd(encodedBytes, widget.text.size(), encodedBytes)
            || encodedBytes > UiDocumentIO::kMaxFileBytes) {
            return false;
        }
    }
    return true;
}

/** Minimal little-endian append-only writer over a byte buffer. */
struct Writer {
    std::vector<std::uint8_t> buf;

    void PutU8(std::uint8_t v) { buf.push_back(v); }
    void PutU32(std::uint32_t v)
    {
        buf.push_back(static_cast<std::uint8_t>(v & 0xffu));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
        buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
        buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
    }
    void PutF32(float v)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        PutU32(bits);
    }
    void PutStr(const std::string& s)
    {
        PutU32(static_cast<std::uint32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }
};

/** Bounds-checked little-endian reader; Ok() latches false on any overrun. */
struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    bool Ok() const { return ok; }

    std::size_t Remaining() const noexcept
    {
        return pos <= size ? size - pos : 0u;
    }

    std::uint8_t GetU8()
    {
        if (Remaining() < 1u) { ok = false; return 0; }
        return data[pos++];
    }
    std::uint32_t GetU32()
    {
        if (Remaining() < 4u) { ok = false; return 0; }
        const std::uint32_t v = static_cast<std::uint32_t>(data[pos])
            | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
            | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
            | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    float GetF32()
    {
        const std::uint32_t bits = GetU32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    std::string GetStr()
    {
        const std::uint32_t len = GetU32();
        if (!ok || len > UiDocumentIO::kMaxWidgetTextBytes
            || static_cast<std::size_t>(len) > Remaining()) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }
};

bool SaveDocument(const UiDocument& document, const std::string& path)
{
    std::size_t encodedBytes = 0;
    if (!ValidateDocument(document, encodedBytes)) {
        Debug::Logger::Error("UI", "cui save: invalid or oversized document for '%s'",
                             path.c_str());
        return false;
    }

    Writer w;
    w.buf.reserve(encodedBytes);
    w.PutU32(kMagic);
    w.PutU32(kVersion);
    w.PutU32(static_cast<std::uint32_t>(document.widgets.size()));
    for (const Widget& widget : document.widgets) {
        w.PutU8(static_cast<std::uint8_t>(widget.kind));
        w.PutU32(widget.id);
        w.PutF32(widget.rect.x);
        w.PutF32(widget.rect.y);
        w.PutF32(widget.rect.width);
        w.PutF32(widget.rect.height);
        w.PutU32(widget.color);
        w.PutU8(static_cast<std::uint8_t>(widget.hAlign));
        w.PutU8(static_cast<std::uint8_t>(widget.vAlign));
        w.PutF32(widget.fontScale);
        w.PutStr(widget.text);
    }

    std::error_code dirError;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, dirError)) {
        std::filesystem::create_directories(parent, dirError);
        if (dirError) {
            Debug::Logger::Error("UI", "cui save: cannot create directory '%s' (%s)",
                                 parent.string().c_str(), dirError.message().c_str());
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        Debug::Logger::Error("UI", "cui save: cannot open '%s' for writing", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(w.buf.data()),
              static_cast<std::streamsize>(w.buf.size()));
    if (!out) {
        Debug::Logger::Error("UI", "cui save: write error on '%s'", path.c_str());
        return false;
    }
    Debug::Logger::Info("UI", "saved '%s' (%zu widgets, %zu bytes)",
                        path.c_str(), document.widgets.size(), w.buf.size());
    return true;
}

bool LoadDocument(UiDocument& document, const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        Debug::Logger::Error("UI", "cui load: cannot open '%s'", path.c_str());
        return false;
    }
    const std::streamsize streamSize = in.tellg();
    if (streamSize < static_cast<std::streamsize>(kHeaderBytes)
        || streamSize > static_cast<std::streamsize>(UiDocumentIO::kMaxFileBytes)) {
        Debug::Logger::Error("UI", "cui load: invalid file size for '%s'", path.c_str());
        return false;
    }
    const auto size = static_cast<std::size_t>(streamSize);
    in.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), streamSize)) {
        Debug::Logger::Error("UI", "cui load: read error on '%s'", path.c_str());
        return false;
    }

    Reader r{bytes.data(), bytes.size()};
    const std::uint32_t magic = r.GetU32();
    const std::uint32_t version = r.GetU32();
    if (magic != kMagic) {
        Debug::Logger::Error("UI", "cui load: bad magic in '%s' (0x%08x != 0x%08x)",
                             path.c_str(), magic, kMagic);
        return false;
    }
    if (version != kVersion) {
        Debug::Logger::Error("UI", "cui load: version %u in '%s' (expected %u)",
                             version, path.c_str(), kVersion);
        return false;
    }

    const std::uint32_t count = r.GetU32();
    if (!r.Ok() || count > UiDocumentIO::kMaxWidgetCount
        || static_cast<std::size_t>(count) > r.Remaining() / kWidgetFixedBytes) {
        Debug::Logger::Error("UI", "cui load: invalid widget count in '%s'", path.c_str());
        return false;
    }

    UiDocument loaded;
    loaded.widgets.reserve(count);
    std::unordered_set<std::uint32_t> buttonIds;
    buttonIds.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Widget widget;
        widget.kind = static_cast<WidgetKind>(r.GetU8());
        widget.id = r.GetU32();
        widget.rect.x = r.GetF32();
        widget.rect.y = r.GetF32();
        widget.rect.width = r.GetF32();
        widget.rect.height = r.GetF32();
        widget.color = r.GetU32();
        widget.hAlign = static_cast<Align>(r.GetU8());
        widget.vAlign = static_cast<Align>(r.GetU8());
        widget.fontScale = r.GetF32();
        widget.text = r.GetStr();
        if (!r.Ok() || !ValidateWidget(widget, buttonIds)) {
            Debug::Logger::Error("UI", "cui load: invalid widget %u in '%s'", i,
                                 path.c_str());
            return false;
        }
        loaded.widgets.push_back(std::move(widget));
    }
    if (r.Remaining() != 0u) {
        Debug::Logger::Error("UI", "cui load: trailing data in '%s'", path.c_str());
        return false;
    }

    document = std::move(loaded);
    Debug::Logger::Info("UI", "loaded '%s' (%u widgets)", path.c_str(), count);
    return true;
}

} // namespace

bool UiDocumentIO::Save(const UiDocument& document, const std::string& path)
{
    try {
        return SaveDocument(document, path);
    } catch (const std::bad_alloc&) {
        Debug::Logger::Error("UI", "cui save: allocation failed for '%s'", path.c_str());
    } catch (const std::length_error&) {
        Debug::Logger::Error("UI", "cui save: allocation size rejected for '%s'", path.c_str());
    }
    return false;
}

bool UiDocumentIO::Load(UiDocument& document, const std::string& path)
{
    try {
        return LoadDocument(document, path);
    } catch (const std::bad_alloc&) {
        Debug::Logger::Error("UI", "cui load: allocation failed for '%s'", path.c_str());
    } catch (const std::length_error&) {
        Debug::Logger::Error("UI", "cui load: allocation size rejected for '%s'", path.c_str());
    }
    return false;
}

} // namespace Concord::UI
