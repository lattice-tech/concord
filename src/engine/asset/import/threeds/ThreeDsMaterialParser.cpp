#include "engine/asset/import/threeds/ThreeDsMaterialParser.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Concord::Asset::ThreeDs {

namespace {

// 3DS chunk ids used by the material parser.
constexpr std::uint16_t kMaterialEntry    = 0xAFFF;
constexpr std::uint16_t kMaterialName     = 0xA000;
constexpr std::uint16_t kMaterialAmbient  = 0xA010;
constexpr std::uint16_t kMaterialDiffuse  = 0xA020;
constexpr std::uint16_t kMaterialSpecular = 0xA030;
constexpr std::uint16_t kMaterialShininess= 0xA040;
constexpr std::uint16_t kMaterialTransp   = 0xA050;
constexpr std::uint16_t kMaterialTexmap   = 0xA200;
constexpr std::uint16_t kMaterialMapname  = 0xA300;

// Color / percentage sub-chunk ids (lib3ds / Autodesk 3DS spec).
// 0x0010 COLOR_F, 0x0011 COLOR_24, 0x0012 LIN_COLOR_24, 0x0013 LIN_COLOR_F.
constexpr std::uint16_t kColorF           = 0x0010;
constexpr std::uint16_t kColor24          = 0x0011;
constexpr std::uint16_t kLinColor24       = 0x0012;
constexpr std::uint16_t kLinColorF        = 0x0013;
constexpr std::uint16_t kIntPercentage    = 0x0030;
constexpr std::uint16_t kFloatPercentage  = 0x0031;

/** Packs three 0..1 floats into a 0xRRGGBBAA color (opaque). */
std::uint32_t ToPackedColor(float r, float g, float b)
{
    const auto clamp = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const auto to8 = [&clamp](float v) {
        return static_cast<std::uint32_t>(clamp(v) * 255.0f + 0.5f);
    };
    return (to8(r) << 24) | (to8(g) << 16) | (to8(b) << 8) | 0xFFu;
}

/**
 * Reads a color from a color chunk (0xA010/0xA020/0xA030), which contains one
 * of the COLOR_24/LIN_COLOR_24/COLOR_F/LIN_COLOR_F sub-chunks. Returns the
 * color as three 0..1 floats; defaults to black when no sub-chunk is found.
 */
void ReadColor(ChunkReader& reader, const Chunk& colorChunk, float& r, float& g, float& b)
{
    r = g = b = 0.75f; // visible default if the chunk is empty/corrupt
    while (reader.HasMore(colorChunk)) {
        const Chunk sub = reader.ReadHeader();
        if (sub.id == kColor24 || sub.id == kLinColor24) {
            const std::uint8_t rb = reader.ReadU8();
            const std::uint8_t gb = reader.ReadU8();
            const std::uint8_t bb = reader.ReadU8();
            r = rb / 255.0f;
            g = gb / 255.0f;
            b = bb / 255.0f;
            reader.Skip(sub);
        } else if (sub.id == kColorF || sub.id == kLinColorF) {
            r = reader.ReadFloat();
            g = reader.ReadFloat();
            b = reader.ReadFloat();
            reader.Skip(sub);
        } else {
            reader.Skip(sub);
        }
    }
}

/**
 * Reads a percentage from a percentage chunk (0xA040/0xA050), which contains
 * either an INT_PERCENTAGE (int16) or FLOAT_PERCENTAGE (float) sub-chunk.
 * Returns a 0..1 normalized value.
 */
float ReadPercentage(ChunkReader& reader, const Chunk& pctChunk)
{
    float value = 0.0f;
    while (reader.HasMore(pctChunk)) {
        const Chunk sub = reader.ReadHeader();
        if (sub.id == kIntPercentage) {
            const std::uint16_t raw = reader.ReadU16();
            value = static_cast<float>(static_cast<std::int16_t>(raw)) / 100.0f;
        } else if (sub.id == kFloatPercentage) {
            value = reader.ReadFloat() / 100.0f;
        }
        reader.Skip(sub);
    }
    return std::clamp(value, 0.0f, 1.0f);
}

/** Reads the diffuse texture filename from a 0xA200 texture-map chunk. */
std::string ReadMapFilename(ChunkReader& reader, const Chunk& mapChunk)
{
    std::string filename;
    while (reader.HasMore(mapChunk)) {
        const Chunk sub = reader.ReadHeader();
        if (sub.id == kMaterialMapname) {
            filename = reader.ReadCString();
        }
        reader.Skip(sub);
    }
    return filename;
}

/**
 * Parses one material entry (0xAFFF) into a ParsedMaterial, dispatching each
 * sub-chunk to the matching reader and skipping unknown ones.
 */
ParsedMaterial ParseMaterialEntry(ChunkReader& reader, const Chunk& entry)
{
    ParsedMaterial mat;
    mat.name = "<unnamed>";

    float diffR = 0.8f, diffG = 0.8f, diffB = 0.8f; // sensible neutral default
    float shininess = 0.0f;
    float transparency = 0.0f;

    while (reader.HasMore(entry)) {
        const Chunk sub = reader.ReadHeader();
        switch (sub.id) {
            case kMaterialName:
                mat.name = reader.ReadCString();
                break;
            case kMaterialDiffuse:
                ReadColor(reader, sub, diffR, diffG, diffB);
                break;
            case kMaterialShininess:
                shininess = ReadPercentage(reader, sub);
                break;
            case kMaterialTransp:
                transparency = ReadPercentage(reader, sub);
                break;
            case kMaterialTexmap:
                mat.desc.textures.albedo.path = ReadMapFilename(reader, sub);
                break;
            default:
                break;
        }
        reader.Skip(sub);
    }

    mat.desc.surface.albedo = ToPackedColor(diffR, diffG, diffB);
    // 3DS shininess percentage (0..1) → GGX roughness. Keep a floor so missing
    // textures on glossy gray walls do not go pure black under sparse lights.
    mat.desc.surface.roughness = std::clamp(1.0f - shininess, 0.25f, 1.0f);
    mat.desc.surface.metallic = 0.0f;
    // Double-sided: 3DS winding is inconsistent across exporters.
    mat.desc.draw.cull = CullMode::None;
    (void)transparency; // engine has no blend pass yet; always opaque
    return mat;
}

} // namespace

std::vector<ParsedMaterial> ParseMaterials(ChunkReader& reader, const Chunk& edit)
{
    std::vector<ParsedMaterial> materials;
    while (reader.HasMore(edit)) {
        const Chunk sub = reader.ReadHeader();
        if (sub.id == kMaterialEntry) {
            materials.push_back(ParseMaterialEntry(reader, sub));
        }
        reader.Skip(sub);
    }
    return materials;
}

} // namespace Concord::Asset::ThreeDs
