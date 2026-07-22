#include "engine/ui/UiDocumentIO.h"

#include "engine/debug/Logger.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Concord::UI {

namespace {

// 'C','U','I','1' as a little-endian tag, plus a version guard.
constexpr std::uint32_t kMagic = 0x31495543u;
constexpr std::uint32_t kVersion = 1u;

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

    std::uint8_t GetU8()
    {
        if (pos + 1 > size) { ok = false; return 0; }
        return data[pos++];
    }
    std::uint32_t GetU32()
    {
        if (pos + 4 > size) { ok = false; return 0; }
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
        if (!ok || pos + len > size) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }
};

} // namespace

bool UiDocumentIO::Save(const UiDocument& document, const std::string& path)
{
    Writer w;
    w.buf.reserve(16 + document.widgets.size() * 32);
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

bool UiDocumentIO::Load(UiDocument& document, const std::string& path)
{
    document.Clear();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        Debug::Logger::Error("UI", "cui load: cannot open '%s'", path.c_str());
        return false;
    }
    const std::streamsize size = in.tellg();
    if (size <= 0) {
        Debug::Logger::Error("UI", "cui load: empty or unreadable '%s'", path.c_str());
        return false;
    }
    in.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
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
    if (!r.Ok()) {
        Debug::Logger::Error("UI", "cui load: truncated header in '%s'", path.c_str());
        return false;
    }
    document.widgets.reserve(count);
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
        if (!r.Ok()) {
            Debug::Logger::Error("UI", "cui load: truncated widget %u in '%s'", i, path.c_str());
            document.Clear();
            return false;
        }
        document.widgets.push_back(std::move(widget));
    }
    Debug::Logger::Info("UI", "loaded '%s' (%u widgets)", path.c_str(), count);
    return true;
}

} // namespace Concord::UI
