#include "engine/asset/cook/CookedMaterial.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

namespace Concord::Asset::CookedMaterial {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x5441464du; // 'MFAT' in file order
constexpr std::uint32_t kVersion = 1;

// The highest valid small-integer value for each serialized enum, used to
// reject an out-of-range byte instead of casting it into an invalid enum.
constexpr std::uint8_t kMaxMaterialModel = 1; // Unlit, Lit
constexpr std::uint8_t kMaxGradientAxis = 2;  // X, Y, Z
constexpr std::uint8_t kMaxBlendMode = 2;     // Opaque, Alpha, Additive
constexpr std::uint8_t kMaxDepthTest = 6;     // Never..Always
constexpr std::uint8_t kMaxCullMode = 2;      // Back, Front, None

void WriteTextures(BinaryWriter& writer, const Material::MaterialTextures& textures)
{
    writer.PutString(textures.albedo.path);
    writer.PutString(textures.normal.path);
    writer.PutString(textures.metallicRoughness.path);
    writer.PutString(textures.emissive.path);
}

} // namespace

std::vector<std::uint8_t> Encode(const Material::MaterialDesc& material)
{
    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);

    writer.PutU8(static_cast<std::uint8_t>(material.model));

    writer.PutU32(material.surface.albedo);
    writer.PutF32(material.surface.metallic);
    writer.PutF32(material.surface.roughness);
    writer.PutF32(material.surface.reflectivity);
    writer.PutU32(material.surface.emissive);
    writer.PutF32(material.surface.emissiveStrength);

    writer.PutU8(material.gradient.enabled ? 1u : 0u);
    writer.PutU32(material.gradient.from);
    writer.PutU32(material.gradient.to);
    writer.PutU8(static_cast<std::uint8_t>(material.gradient.axis));

    WriteTextures(writer, material.textures);

    writer.PutU8(static_cast<std::uint8_t>(material.draw.blend));
    writer.PutU8(static_cast<std::uint8_t>(material.draw.depthTest));
    writer.PutU8(material.draw.depthWrite ? 1u : 0u);
    writer.PutU8(static_cast<std::uint8_t>(material.draw.cull));
    writer.PutI32(material.draw.priority);

    writer.PutU8(material.planarReflection ? 1u : 0u);
    return writer.Take();
}

std::optional<Material::MaterialDesc> Decode(const std::uint8_t* data,
                                             std::size_t size,
                                             const CookedMaterialLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    Material::MaterialDesc material;

    const std::uint8_t model = reader.GetU8();
    if (model > kMaxMaterialModel) {
        return std::nullopt;
    }
    material.model = static_cast<Material::MaterialModel>(model);

    material.surface.albedo = reader.GetU32();
    material.surface.metallic = reader.GetF32();
    material.surface.roughness = reader.GetF32();
    material.surface.reflectivity = reader.GetF32();
    material.surface.emissive = reader.GetU32();
    material.surface.emissiveStrength = reader.GetF32();

    material.gradient.enabled = reader.GetU8() != 0;
    material.gradient.from = reader.GetU32();
    material.gradient.to = reader.GetU32();
    const std::uint8_t axis = reader.GetU8();
    if (axis > kMaxGradientAxis) {
        return std::nullopt;
    }
    material.gradient.axis = static_cast<Material::GradientAxis>(axis);

    const std::size_t maxPath = limits.maxTexturePathBytes;
    material.textures.albedo.path = reader.GetString(maxPath);
    material.textures.normal.path = reader.GetString(maxPath);
    material.textures.metallicRoughness.path = reader.GetString(maxPath);
    material.textures.emissive.path = reader.GetString(maxPath);

    const std::uint8_t blend = reader.GetU8();
    const std::uint8_t depthTest = reader.GetU8();
    const std::uint8_t depthWrite = reader.GetU8();
    const std::uint8_t cull = reader.GetU8();
    material.draw.priority = reader.GetI32();
    const std::uint8_t planar = reader.GetU8();

    if (!reader.Ok() || blend > kMaxBlendMode || depthTest > kMaxDepthTest
        || cull > kMaxCullMode) {
        return std::nullopt;
    }
    material.draw.blend = static_cast<Material::BlendMode>(blend);
    material.draw.depthTest = static_cast<DepthTest>(depthTest);
    material.draw.depthWrite = depthWrite != 0;
    material.draw.cull = static_cast<CullMode>(cull);
    material.planarReflection = planar != 0;

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return material;
}

} // namespace Concord::Asset::CookedMaterial
