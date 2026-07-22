#include "engine/asset/import/gltf/GltfBufferReader.h"

#include "engine/asset/import/ImportPaths.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace Concord::Asset::Gltf {

int ComponentSize(int componentType) noexcept
{
    switch (componentType) {
        case 5120: case 5121: return 1; // BYTE / UNSIGNED_BYTE
        case 5122: case 5123: return 2; // SHORT / UNSIGNED_SHORT
        case 5125: return 4;            // UNSIGNED_INT
        case 5126: return 4;            // FLOAT
        default: return 0;
    }
}

int TypeCount(std::string_view type) noexcept
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 1;
}

bool TryReadSize(const JsonValue& object,
                 std::string_view key,
                 std::size_t fallback,
                 std::size_t& value) noexcept
{
    const JsonValue* member = object.Find(key);
    if (member == nullptr) {
        value = fallback;
        return true;
    }
    if (!member->IsNumber() || !std::isfinite(member->number) || member->number < 0.0 ||
        std::trunc(member->number) != member->number ||
        member->number >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value = static_cast<std::size_t>(member->number);
    return true;
}

bool TryAddSize(std::size_t left, std::size_t right, std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool TryMultiplySize(std::size_t left, std::size_t right, std::size_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

std::array<float, 16> ReadAccessor(const JsonValue& accessor,
                                   const std::vector<std::vector<std::uint8_t>>& buffers,
                                   const std::vector<JsonValue>& bufferViews,
                                   std::size_t elementIndex)
{
    std::array<float, 16> out{};
    out.fill(0.0f);

    std::size_t bufferViewIndex = bufferViews.size();
    if (!TryReadSize(accessor, "bufferView", bufferViews.size(), bufferViewIndex) ||
        bufferViewIndex >= bufferViews.size()) {
        return out;
    }
    const JsonValue& bv = bufferViews[bufferViewIndex];

    std::size_t bufferIndex = 0;
    if (!TryReadSize(bv, "buffer", 0, bufferIndex) || bufferIndex >= buffers.size()) {
        return out;
    }
    const std::vector<std::uint8_t>& buf = buffers[bufferIndex];

    std::size_t bufferViewOffset = 0;
    std::size_t accessorOffset = 0;
    std::size_t byteOffset = 0;
    if (!TryReadSize(bv, "byteOffset", 0, bufferViewOffset) ||
        !TryReadSize(accessor, "byteOffset", 0, accessorOffset) ||
        !TryAddSize(bufferViewOffset, accessorOffset, byteOffset)) {
        return out;
    }

    std::size_t componentTypeValue = 5126;
    if (!TryReadSize(accessor, "componentType", 5126, componentTypeValue) ||
        componentTypeValue > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return out;
    }
    const int componentType = static_cast<int>(componentTypeValue);
    const int compSize = ComponentSize(componentType);
    const int count = TypeCount(accessor.StrOr("type", "SCALAR"));
    if (compSize <= 0 || count <= 0) {
        return out;
    }
    std::size_t elementByteSize = 0;
    if (!TryMultiplySize(static_cast<std::size_t>(compSize),
                         static_cast<std::size_t>(count),
                         elementByteSize)) {
        return out;
    }

    std::size_t stride = elementByteSize;
    if (!TryReadSize(bv, "byteStride", elementByteSize, stride) || stride < elementByteSize) {
        return out;
    }

    std::size_t elementOffset = 0;
    std::size_t offset = 0;
    std::size_t end = 0;
    if (!TryMultiplySize(elementIndex, stride, elementOffset) ||
        !TryAddSize(byteOffset, elementOffset, offset) ||
        !TryAddSize(offset, elementByteSize, end) || end > buf.size()) {
        return out;
    }
    const std::uint8_t* p = buf.data() + offset;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* e = p + i * compSize;
        switch (componentType) {
            case 5126: { float v; std::memcpy(&v, e, 4); out[i] = v; break; }
            case 5123: { std::uint16_t v; std::memcpy(&v, e, 2); out[i] = static_cast<float>(v); break; }
            case 5122: { std::int16_t v; std::memcpy(&v, e, 2); out[i] = static_cast<float>(v); break; }
            case 5121: { std::uint8_t v; std::memcpy(&v, e, 1); out[i] = static_cast<float>(v); break; }
            case 5120: { std::int8_t v; std::memcpy(&v, e, 1); out[i] = static_cast<float>(v); break; }
            case 5125: { std::uint32_t v; std::memcpy(&v, e, 4); out[i] = static_cast<float>(v); break; }
            default: break;
        }
    }
    const JsonValue* normalizedValue = accessor.Find("normalized");
    const bool normalized = normalizedValue != nullptr &&
        ((normalizedValue->type == JsonValue::Type::Bool && normalizedValue->boolean) ||
         (normalizedValue->IsNumber() && normalizedValue->number == 1.0));
    if (normalized) {
        switch (componentType) {
            case 5120: for (int i = 0; i < count; ++i) out[i] = std::max(out[i] / 127.0f, -1.0f); break;
            case 5121: for (int i = 0; i < count; ++i) out[i] = out[i] / 255.0f; break;
            case 5122: for (int i = 0; i < count; ++i) out[i] = std::max(out[i] / 32767.0f, -1.0f); break;
            case 5123: for (int i = 0; i < count; ++i) out[i] = out[i] / 65535.0f; break;
            default: break;
        }
    }
    return out;
}

bool IsDataUri(std::string_view uri)
{
    return uri.size() >= 5 && uri.compare(0, 5, "data:") == 0;
}

std::vector<std::uint8_t> DecodeDataUri(std::string_view uri)
{
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos) {
        return {};
    }
    const std::string_view b64 = uri.substr(comma + 1);
    static const std::string kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out;
    out.reserve((b64.size() / 4) * 3);
    int val = 0;
    int bits = 0;
    for (char c : b64) {
        if (c == '=') {
            break;
        }
        const std::size_t idx = kTable.find(c);
        if (idx == std::string::npos) {
            continue; // skip whitespace
        }
        val = (val << 6) | static_cast<int>(idx);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((val >> bits) & 0xFF));
        }
    }
    return out;
}

std::vector<std::uint8_t> LoadBuffer(const JsonValue& buffer,
                                     const std::string& dir,
                                     const std::vector<std::uint8_t>& glbBin)
{
    const std::string uri = buffer.StrOr("uri", "");
    if (uri.empty()) {
        return glbBin; // GLB embeds the BIN chunk as buffer 0 with no uri
    }
    if (IsDataUri(uri)) {
        return DecodeDataUri(uri);
    }
    const std::string path = Paths::ResolveWithin(dir, uri);
    if (path.empty()) {
        Debug::Logger::Warn("Asset", "glTF: rejected buffer URI '%s'", uri.c_str());
        return {};
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Debug::Logger::Warn("Asset", "glTF: cannot open buffer '%s'", path.c_str());
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

} // namespace Concord::Asset::Gltf
