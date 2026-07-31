#ifndef CONCORD_COOKEDMODEL_H
#define CONCORD_COOKEDMODEL_H

#include "engine/asset/cook/CookedMaterial.h"
#include "engine/asset/cook/CookedMesh.h"
#include "engine/asset/import/ImportedModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings enforced when decoding an untrusted cooked model. */
struct CookedModelLimits {
    std::uint32_t maxSubMeshes = 1024u;
    std::uint32_t maxNameBytes = 1024u;
    CookedMeshLimits mesh{};
    CookedMaterialLimits material{};
};

/**
 * @brief Versioned little-endian binary container for a cooked static model.
 *
 * The runtime-ready form of one imported model file: the model's name, its
 * axis-aligned bounds, and each sub-mesh as an embedded CookedMesh blob paired
 * with an embedded CookedMaterial blob. Encoding is deterministic so
 * re-cooking an identical ImportedModel yields byte-identical output.
 *
 * M1 scope is static geometry: skeletons and animation clips are not stored,
 * so a skinned model must not be encoded through this container (the cook
 * producer keeps skinned sources on the passthrough path and the runtime
 * falls back to live import for them).
 */
namespace CookedModel {

/** True when the first bytes carry this container's magic/version header. */
bool LooksLikeCookedModel(const std::uint8_t* data, std::size_t size) noexcept;

/**
 * Encodes the static portion of `model` to the deterministic cooked byte
 * form. Returns an empty vector when the model is skinned or has no geometry.
 */
std::vector<std::uint8_t> Encode(const ImportedModel& model);

/**
 * Decodes a cooked model blob. Returns nullopt on bad magic/version,
 * truncation, trailing bytes, a rejected embedded mesh/material blob, or a
 * count above `limits`. The result carries an empty skeleton and no clips.
 */
std::optional<ImportedModel> Decode(const std::uint8_t* data, std::size_t size,
                                    const CookedModelLimits& limits = {});

} // namespace CookedModel

} // namespace Concord::Asset

#endif // CONCORD_COOKEDMODEL_H
