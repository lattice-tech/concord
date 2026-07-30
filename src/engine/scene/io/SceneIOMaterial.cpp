#include "engine/scene/io/SceneIOPayload.h"

#include "engine/material/BlendMode.h"

namespace Concord::Detail::SceneIo {

void WriteMaterial(Writer& writer, const Material::MaterialDesc& material)
{
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
    writer.PutString(material.textures.albedo.path);
    writer.PutString(material.textures.normal.path);
    writer.PutString(material.textures.metallicRoughness.path);
    writer.PutString(material.textures.emissive.path);
    writer.PutU8(static_cast<std::uint8_t>(material.draw.depthTest));
    writer.PutU8(material.draw.depthWrite ? 1u : 0u);
    writer.PutU8(static_cast<std::uint8_t>(material.draw.cull));
    writer.PutU8(static_cast<std::uint8_t>(material.draw.blend));
    writer.PutI32(material.draw.priority);
    writer.PutU8(material.planarReflection ? 1u : 0u);
}

Material::MaterialDesc ReadMaterial(Reader& reader)
{
    Material::MaterialDesc material;
    material.model = static_cast<Material::MaterialModel>(reader.GetU8());
    material.surface.albedo = reader.GetU32();
    material.surface.metallic = reader.GetF32();
    material.surface.roughness = reader.GetF32();
    material.surface.reflectivity = reader.GetF32();
    material.surface.emissive = reader.GetU32();
    material.surface.emissiveStrength = reader.GetF32();
    material.gradient.enabled = reader.GetU8() != 0;
    material.gradient.from = reader.GetU32();
    material.gradient.to = reader.GetU32();
    material.gradient.axis = static_cast<Material::GradientAxis>(reader.GetU8());
    material.textures.albedo.path = reader.GetString();
    material.textures.normal.path = reader.GetString();
    material.textures.metallicRoughness.path = reader.GetString();
    material.textures.emissive.path = reader.GetString();
    material.draw.depthTest = static_cast<DepthTest>(reader.GetU8());
    material.draw.depthWrite = reader.GetU8() != 0;
    material.draw.cull = static_cast<CullMode>(reader.GetU8());
    material.draw.blend = static_cast<Material::BlendMode>(reader.GetU8());
    material.draw.priority = reader.GetI32();
    material.planarReflection = reader.GetU8() != 0;
    return material;
}

} // namespace Concord::Detail::SceneIo
