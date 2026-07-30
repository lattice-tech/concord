#ifndef CONCORD_COOKEDSCENEDATA_H
#define CONCORD_COOKEDSCENEDATA_H

#include "engine/asset/cook/CookedScenePayload.h"
#include "engine/environment/EnvironmentSettings.h"
#include "engine/object/PersistentObjectId.h"
#include "engine/object/Transform.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings shared by untrusted cooked scene and prefab graphs. */
struct CookedSceneGraphLimits {
    std::size_t maxFileBytes = 64u * 1024u * 1024u;
    std::uint32_t maxNodes = 65'536u;
    std::uint32_t maxHierarchyDepth = 1024u;
    std::uint32_t maxNodeRecordBytes = 4u * 1024u * 1024u;
    std::uint32_t maxAssetKeyBytes = 4096u;
    std::uint32_t maxModelPartsPerNode = 4096u;
    std::uint32_t maxAnimationsPerNode = 4096u;
    std::uint32_t maxAssetReferences = 262'144u;
    std::uint32_t maxParticleForceFields = 1024u;
    std::uint32_t maxParticleBursts = 16'384u;
    std::uint32_t maxParticleCapacity = 65'536u;
    std::uint32_t maxTotalParticleCapacity = 262'144u;
};

/** Transform, render settings, and typed payload shared by scene and prefab nodes. */
struct CookedNodeData {
    CookedNodeSettings settings{};
    Transform localTransform{};
    CookedNodePayload payload{};
};

/** One node in a cooked Scene, using the Scene's persistent identity domain. */
struct CookedSceneNode {
    Object::PersistentObjectId id{};
    Object::PersistentObjectId parentId{};
    CookedNodeData data{};
};

/** Runtime-ready Scene data with no development source paths. */
struct CookedSceneData {
    EnvironmentSettings environment{};
    Object::PersistentObjectId activeCameraId{};
    std::vector<CookedSceneNode> nodes;
};

/** Stable node key local to one Prefab asset; never passed to Scene::Find. */
struct PrefabNodeId {
    std::uint64_t value = 0;

    constexpr bool IsValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(PrefabNodeId, PrefabNodeId) noexcept = default;
};

/** One immutable Prefab template node in the Prefab-local identity domain. */
struct CookedPrefabNode {
    PrefabNodeId id{};
    PrefabNodeId parentId{};
    CookedNodeData data{};
};

/** Immutable Prefab node template graph; instancing and overrides are separate. */
struct CookedPrefabData {
    std::vector<CookedPrefabNode> nodes;
};

} // namespace Concord::Asset

#endif // CONCORD_COOKEDSCENEDATA_H
