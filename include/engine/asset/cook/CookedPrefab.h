#ifndef CONCORD_COOKEDPREFAB_H
#define CONCORD_COOKEDPREFAB_H

#include "engine/asset/cook/CookedSceneData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset::CookedPrefab {

/** Validates an immutable Prefab-local node template graph. */
bool Validate(const CookedPrefabData& prefab,
              const CookedSceneGraphLimits& limits = {});

/**
 * Encodes a deterministic cooked Prefab, sorting records by Prefab-local ID.
 * @throws std::invalid_argument when the Prefab violates the format contract.
 */
std::vector<std::uint8_t> Encode(
    const CookedPrefabData& prefab,
    const CookedSceneGraphLimits& limits = {});

/** Decodes a cooked Prefab blob, returning nullopt on any format violation. */
std::optional<CookedPrefabData> Decode(
    const std::uint8_t* data, std::size_t size,
    const CookedSceneGraphLimits& limits = {});

} // namespace Concord::Asset::CookedPrefab

#endif // CONCORD_COOKEDPREFAB_H
