#ifndef CONCORD_COOKEDSCENE_H
#define CONCORD_COOKEDSCENE_H

#include "engine/asset/cook/CookedSceneData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset::CookedScene {

/** Validates a runtime scene graph and all referenced cooked asset identities. */
bool Validate(const CookedSceneData& scene,
              const CookedSceneGraphLimits& limits = {});

/**
 * Encodes a deterministic cooked scene, sorting records by persistent ID.
 * @throws std::invalid_argument when the scene violates the format contract.
 */
std::vector<std::uint8_t> Encode(
    const CookedSceneData& scene,
    const CookedSceneGraphLimits& limits = {});

/** Decodes a cooked scene blob, returning nullopt on any format violation. */
std::optional<CookedSceneData> Decode(
    const std::uint8_t* data, std::size_t size,
    const CookedSceneGraphLimits& limits = {});

} // namespace Concord::Asset::CookedScene

#endif // CONCORD_COOKEDSCENE_H
