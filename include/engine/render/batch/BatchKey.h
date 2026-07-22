#ifndef CONCORD_BATCHKEY_H
#define CONCORD_BATCHKEY_H

#include "engine/render/mesh/MeshHandle.h"
#include "engine/render/frame/RenderEffect.h"

#include <cstdint>
#include <functional>

namespace Concord {

/**
 * Packs a MeshHandle into a single 64-bit value that is orderable and
 * hashable as one piece.
 *
 * The slot index occupies the high 32 bits and the generation the low 32,
 * so two handles to the same slot at different generations compare distinct
 * (a stale handle never matches a live one), while two live handles to the
 * same mesh collapse to the same key — exactly what the batcher needs when
 * grouping draws that share a vertex/index buffer pair.
 */
inline std::uint64_t PackMesh(MeshHandle handle) noexcept
{
    return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
}

/**
 * Composite key identifying one batch the render batcher is forming: the
 * mesh whose buffers all instances share, plus the material hash whose
 * uniforms they all share. Two draws collapse into the same batch iff their
 * BatchKeys are equal AND their materials compare equal under operator==
 * (the hash pins the candidate, operator== confirms it — see RenderBatcher).
 */
struct BatchKey {
    /** Packed MeshHandle (see PackMesh). */
    std::uint64_t mesh = 0;

    /** 64-bit hash of the resolved material (see HashMaterial). */
    std::uint64_t material = 0;

    /** Shader path shared by every draw in the batch. */
    RenderEffect effect = RenderEffect::Mesh;

    /** Whether this batch samples the real-time reflection cubemap. */
    bool realtimeReflection = false;
};

/** Two batch keys are equal iff both halves match. */
inline bool operator==(const BatchKey& lhs, const BatchKey& rhs) noexcept
{
    return lhs.mesh == rhs.mesh && lhs.material == rhs.material && lhs.effect == rhs.effect
        && lhs.realtimeReflection == rhs.realtimeReflection;
}

/**
 * Hash functor for std::unordered_map<BatchKey, ...>.
 *
 * Folds the two 64-bit halves with a mixing step borrowed from MurmurHash3
 * so collisions stay rare even across wildly varying mesh indices; combined
 * with the collision check on operator== in the batcher this keeps the
 * lookup path linear in the true number of distinct (mesh, material) pairs.
 */
struct BatchKeyHash {
    std::size_t operator()(const BatchKey& key) const noexcept
    {
        const std::uint64_t mix = key.mesh * 0x9E3779B97F4A7C15ULL
            ^ key.material
            ^ static_cast<std::uint64_t>(key.effect) * 0xC2B2AE3D27D4EB4FULL
            ^ static_cast<std::uint64_t>(key.realtimeReflection) * 0x165667B19E3779F9ULL;
        return static_cast<std::size_t>(mix ^ (mix >> 32));
    }
};

} // namespace Concord

#endif // CONCORD_BATCHKEY_H
