#ifndef CONCORD_GLTF_SKINBUILDER_H
#define CONCORD_GLTF_SKINBUILDER_H

#include "engine/animation/clip/SkeletalClip.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/asset/import/gltf/GltfJson.h"
#include "engine/render/mesh/MeshData.h"

#include <cstdint>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Builds a Concord skeleton from a glTF `skin`.
 *
 * The skin's `joints` array lists node indices; bone i corresponds to
 * joints[i]. Each bone takes its bind-local transform from that node's TRS, its
 * inverse-bind matrix from the skin's `inverseBindMatrices` accessor (a MAT4
 * per joint, column-major — used as-is), and its parent from the node
 * hierarchy (the joint whose child list contains this joint's node), or -1 when
 * the parent is outside the skin.
 *
 * @param nodeToBone Filled to map every node index to its bone index (or -1),
 *        so animation channels targeting a node can find its bone.
 * @return The built skeleton (empty if the skin has no joints).
 */
Animation::Skeleton BuildSkeleton(const JsonValue& skin,
                                  const std::vector<JsonValue>& nodes,
                                  const std::vector<JsonValue>& accessors,
                                  const std::vector<std::vector<std::uint8_t>>& buffers,
                                  const std::vector<JsonValue>& bufferViews,
                                  std::vector<int>& nodeToBone);

/**
 * Reads a primitive's JOINTS_0 / WEIGHTS_0 attributes into `geometry`'s
 * boneIndices / boneWeights (weights normalised to sum to 1). A no-op if the
 * primitive lacks skin attributes. `geometry.positions` must already be sized.
 */
void ReadSkinAttributes(const JsonValue& primitive,
                        const std::vector<JsonValue>& accessors,
                        const std::vector<std::vector<std::uint8_t>>& buffers,
                        const std::vector<JsonValue>& bufferViews,
                        MeshData& geometry);

/**
 * Builds a skeletal clip from one glTF `animation`: each channel's sampler
 * (time input + value output) becomes keyframes on the target bone's
 * translation/rotation/scale track. LINEAR and STEP interpolation are treated
 * as linear/slerp; CUBICSPLINE keys use their central value (tangents dropped).
 * Channels targeting nodes outside the skeleton (nodeToBone == -1) are skipped.
 */
Animation::SkeletalClip BuildSkeletalClip(const JsonValue& animation,
                                          const std::vector<JsonValue>& accessors,
                                          const std::vector<std::vector<std::uint8_t>>& buffers,
                                          const std::vector<JsonValue>& bufferViews,
                                          const std::vector<int>& nodeToBone);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_SKINBUILDER_H
