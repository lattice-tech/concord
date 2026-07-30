#ifndef CONCORD_ASSETTYPE_H
#define CONCORD_ASSETTYPE_H

#include <cstdint>
#include <string_view>

namespace Concord::Asset {

/**
 * @brief The kind of content a cooked asset carries.
 *
 * The type is part of an asset's identity: the same project-relative path used
 * for two different kinds of content (for example a mesh and its material
 * sidecar) produces two distinct AssetIds. `Unknown` is reserved for the
 * invalid/default identity and never names real content.
 */
enum class AssetType : std::uint8_t {
    Unknown = 0,
    Mesh,
    Material,
    Texture,
    Skeleton,
    Animation,
    Audio,
    UiDocument,
    Scene,
    Prefab,
};

/** Stable lowercase token for an asset type, used in identity hashing and logs. */
constexpr std::string_view AssetTypeName(AssetType type) noexcept
{
    switch (type) {
        case AssetType::Mesh:       return "mesh";
        case AssetType::Material:   return "material";
        case AssetType::Texture:    return "texture";
        case AssetType::Skeleton:   return "skeleton";
        case AssetType::Animation:  return "animation";
        case AssetType::Audio:      return "audio";
        case AssetType::UiDocument: return "ui";
        case AssetType::Scene:      return "scene";
        case AssetType::Prefab:     return "prefab";
        case AssetType::Unknown:    break;
    }
    return "unknown";
}

/**
 * The inverse of AssetTypeName: maps a stable token back to its type, or
 * AssetType::Unknown for an unrecognized token. Used when reconstructing an
 * identity from a serialized manifest key.
 */
constexpr AssetType AssetTypeFromName(std::string_view token) noexcept
{
    if (token == "mesh")       return AssetType::Mesh;
    if (token == "material")   return AssetType::Material;
    if (token == "texture")    return AssetType::Texture;
    if (token == "skeleton")   return AssetType::Skeleton;
    if (token == "animation")  return AssetType::Animation;
    if (token == "audio")      return AssetType::Audio;
    if (token == "ui")         return AssetType::UiDocument;
    if (token == "scene")      return AssetType::Scene;
    if (token == "prefab")     return AssetType::Prefab;
    return AssetType::Unknown;
}

} // namespace Concord::Asset

#endif // CONCORD_ASSETTYPE_H
