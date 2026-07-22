#include "engine/asset/import/gltf/GltfMaterialBuilder.h"

#include "engine/asset/import/gltf/GltfImageResolver.h"

#include <cstdint>

namespace Concord::Asset::Gltf {

namespace {

/** Converts three 0..1 floats to a packed 0xRRGGBBAA color (opaque). */
std::uint32_t ToPackedColor(float r, float g, float b, float a = 1.0f)
{
    const auto clamp = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const auto to8 = [&clamp](float v) {
        return static_cast<std::uint32_t>(clamp(v) * 255.0f + 0.5f);
    };
    return (to8(r) << 24) | (to8(g) << 16) | (to8(b) << 8) | to8(a);
}

} // namespace

Material::MaterialDesc BuildMaterial(const JsonValue& mat,
                                     const std::vector<JsonValue>& textures,
                                     const std::vector<JsonValue>& images,
                                     const std::vector<std::vector<std::uint8_t>>& buffers,
                                     const std::vector<JsonValue>& bufferViews,
                                     const std::string& dir)
{
    Material::MaterialDesc desc;
    const JsonValue* pbr = mat.Find("pbrMetallicRoughness");
    if (pbr != nullptr) {
        if (const JsonValue* bc = pbr->Find("baseColorFactor"); bc != nullptr && bc->IsArray()) {
            if (bc->array.size() >= 3) {
                const float r = static_cast<float>(bc->array[0].number);
                const float g = static_cast<float>(bc->array[1].number);
                const float b = static_cast<float>(bc->array[2].number);
                float a = 1.0f;
                if (bc->array.size() >= 4) a = static_cast<float>(bc->array[3].number);
                desc.surface.albedo = ToPackedColor(r, g, b, a);
            }
        }
        desc.surface.metallic = static_cast<float>(pbr->NumOr("metallicFactor", 0.0));
        desc.surface.roughness = static_cast<float>(pbr->NumOr("roughnessFactor", 0.5));
        if (const JsonValue* bct = pbr->Find("baseColorTexture"); bct != nullptr) {
            const int idx = bct->IntOr("index", -1);
            if (idx >= 0 && idx < static_cast<int>(textures.size())) {
                desc.textures.albedo.path =
                    ResolveTexturePath(textures[idx], images, buffers, bufferViews, dir);
            }
        }
        if (const JsonValue* mrt = pbr->Find("metallicRoughnessTexture"); mrt != nullptr) {
            const int idx = mrt->IntOr("index", -1);
            if (idx >= 0 && idx < static_cast<int>(textures.size())) {
                desc.textures.metallicRoughness.path =
                    ResolveTexturePath(textures[idx], images, buffers, bufferViews, dir);
            }
        }
    }
    if (const JsonValue* nt = mat.Find("normalTexture"); nt != nullptr) {
        const int idx = nt->IntOr("index", -1);
        if (idx >= 0 && idx < static_cast<int>(textures.size())) {
            desc.textures.normal.path =
                ResolveTexturePath(textures[idx], images, buffers, bufferViews, dir);
        }
    }
    if (const JsonValue* et = mat.Find("emissiveTexture"); et != nullptr) {
        const int idx = et->IntOr("index", -1);
        if (idx >= 0 && idx < static_cast<int>(textures.size())) {
            desc.textures.emissive.path =
                ResolveTexturePath(textures[idx], images, buffers, bufferViews, dir);
            desc.surface.emissiveStrength = 1.0f;
        }
    }
    if (const JsonValue* ef = mat.Find("emissiveFactor"); ef != nullptr && ef->IsArray()) {
        if (ef->array.size() >= 3) {
            const float r = static_cast<float>(ef->array[0].number);
            const float g = static_cast<float>(ef->array[1].number);
            const float b = static_cast<float>(ef->array[2].number);
            desc.surface.emissive = ToPackedColor(r, g, b);
            desc.surface.emissiveStrength = 1.0f;
        }
    }
    // glTF is double-sided by default only when "doubleSided" is true.
    const bool doubleSided = mat.IntOr("doubleSided", 0) != 0;
    if (doubleSided) {
        desc.draw.cull = CullMode::None;
    }
    return desc;
}

} // namespace Concord::Asset::Gltf
