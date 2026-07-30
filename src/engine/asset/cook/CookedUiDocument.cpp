#include "engine/asset/cook/CookedUiDocument.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Concord::Asset::CookedUiDocument {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x31495543u; // 'CUI1' in file order
/** v1: panels/labels/buttons. v2 appends each widget's image texture path. */
constexpr std::uint32_t kVersion1 = 1u;
constexpr std::uint32_t kVersion = 2u;
constexpr std::size_t kHeaderBytes = sizeof(std::uint32_t) * 3u;
constexpr std::size_t kWidgetFixedBytesV1 = 35u;
/** v2 adds the texture path's 4-byte length prefix to every widget record. */
constexpr std::size_t kWidgetFixedBytes = kWidgetFixedBytesV1 + sizeof(std::uint32_t);

bool IsValid(UI::WidgetKind kind) noexcept
{
    return kind == UI::WidgetKind::Panel || kind == UI::WidgetKind::Label
        || kind == UI::WidgetKind::Button || kind == UI::WidgetKind::Image;
}

bool IsValid(UI::Align align) noexcept
{
    return align == UI::Align::Start || align == UI::Align::Center
        || align == UI::Align::End;
}

bool Accumulate(std::size_t amount, std::size_t limit,
                std::size_t& total) noexcept
{
    if (amount > limit || total > limit - amount) {
        return false;
    }
    total += amount;
    return true;
}

bool ValidateWidget(const UI::Widget& widget,
                    const CookedUiDocumentLimits& limits,
                    std::unordered_set<std::uint32_t>& buttonIds)
{
    if (!IsValid(widget.kind) || !IsValid(widget.hAlign)
        || !IsValid(widget.vAlign)
        || !std::isfinite(widget.rect.x) || !std::isfinite(widget.rect.y)
        || !std::isfinite(widget.rect.width)
        || !std::isfinite(widget.rect.height)
        || !std::isfinite(widget.fontScale) || widget.fontScale <= 0.0f
        || widget.rect.width <= 0.0f
        || widget.text.size() > limits.maxWidgetTextBytes
        || widget.texture.size() > limits.maxWidgetTextBytes) {
        return false;
    }

    // An image is defined by its source path; every other kind must leave the
    // field empty so a stray path can never silently become a drawn quad.
    if (widget.kind == UI::WidgetKind::Image) {
        if (widget.texture.empty()) {
            return false;
        }
    } else if (!widget.texture.empty()) {
        return false;
    }

    // A zero-height label is the established intrinsic single-line shorthand.
    if ((widget.kind == UI::WidgetKind::Label && widget.rect.height < 0.0f)
        || (widget.kind != UI::WidgetKind::Label
            && widget.rect.height <= 0.0f)) {
        return false;
    }
    if (widget.kind == UI::WidgetKind::Button) {
        return widget.id != 0u && buttonIds.insert(widget.id).second;
    }
    return widget.id == 0u;
}

void WriteWidget(BinaryWriter& writer, const UI::Widget& widget)
{
    writer.PutU8(static_cast<std::uint8_t>(widget.kind));
    writer.PutU32(widget.id);
    writer.PutF32(widget.rect.x);
    writer.PutF32(widget.rect.y);
    writer.PutF32(widget.rect.width);
    writer.PutF32(widget.rect.height);
    writer.PutU32(widget.color);
    writer.PutU8(static_cast<std::uint8_t>(widget.hAlign));
    writer.PutU8(static_cast<std::uint8_t>(widget.vAlign));
    writer.PutF32(widget.fontScale);
    writer.PutString(widget.text);
    writer.PutString(widget.texture);
}

UI::Widget ReadWidget(BinaryReader& reader, std::uint32_t version,
                      const CookedUiDocumentLimits& limits)
{
    UI::Widget widget;
    widget.kind = static_cast<UI::WidgetKind>(reader.GetU8());
    widget.id = reader.GetU32();
    widget.rect.x = reader.GetF32();
    widget.rect.y = reader.GetF32();
    widget.rect.width = reader.GetF32();
    widget.rect.height = reader.GetF32();
    widget.color = reader.GetU32();
    widget.hAlign = static_cast<UI::Align>(reader.GetU8());
    widget.vAlign = static_cast<UI::Align>(reader.GetU8());
    widget.fontScale = reader.GetF32();
    widget.text = reader.GetString(limits.maxWidgetTextBytes);
    if (version >= kVersion) {
        widget.texture = reader.GetString(limits.maxWidgetTextBytes);
    }
    return widget;
}

} // namespace

bool Validate(const UI::UiDocument& document,
              const CookedUiDocumentLimits& limits)
{
    if (document.widgets.size() > limits.maxWidgets
        || limits.maxFileBytes < kHeaderBytes) {
        return false;
    }

    std::size_t encodedBytes = kHeaderBytes;
    std::unordered_set<std::uint32_t> buttonIds;
    buttonIds.reserve(document.widgets.size());
    for (const UI::Widget& widget : document.widgets) {
        if (!ValidateWidget(widget, limits, buttonIds)
            || !Accumulate(kWidgetFixedBytes, limits.maxFileBytes, encodedBytes)
            || !Accumulate(widget.text.size(), limits.maxFileBytes, encodedBytes)
            || !Accumulate(widget.texture.size(), limits.maxFileBytes,
                           encodedBytes)) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> Encode(const UI::UiDocument& document,
                                 const CookedUiDocumentLimits& limits)
{
    if (!Validate(document, limits)) {
        throw std::invalid_argument("UI document violates the cooked format contract");
    }

    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutU32(static_cast<std::uint32_t>(document.widgets.size()));
    for (const UI::Widget& widget : document.widgets) {
        WriteWidget(writer, widget);
    }
    return writer.Take();
}

std::optional<UI::UiDocument> Decode(
    const std::uint8_t* data, std::size_t size,
    const CookedUiDocumentLimits& limits)
{
    if (data == nullptr || size < kHeaderBytes || size > limits.maxFileBytes) {
        return std::nullopt;
    }

    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic) {
        return std::nullopt;
    }
    const std::uint32_t version = reader.GetU32();
    if (version != kVersion && version != kVersion1) {
        return std::nullopt;
    }
    const std::size_t widgetFixedBytes =
        version >= kVersion ? kWidgetFixedBytes : kWidgetFixedBytesV1;
    const std::uint32_t count = reader.GetU32();
    if (!reader.Ok() || count > limits.maxWidgets
        || count > reader.Remaining() / widgetFixedBytes) {
        return std::nullopt;
    }

    UI::UiDocument document;
    document.widgets.reserve(count);
    std::unordered_set<std::uint32_t> buttonIds;
    buttonIds.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        UI::Widget widget = ReadWidget(reader, version, limits);
        if (!reader.Ok() || !ValidateWidget(widget, limits, buttonIds)) {
            return std::nullopt;
        }
        document.widgets.push_back(std::move(widget));
    }
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return document;
}

} // namespace Concord::Asset::CookedUiDocument
