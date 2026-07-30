#ifndef CONCORD_COOKEDMESH_H
#define CONCORD_COOKEDMESH_H

#include "engine/render/mesh/MeshData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings enforced when decoding an untrusted cooked mesh. */
struct CookedMeshLimits {
    std::uint32_t maxVertices = 4'000'000u;
    std::uint32_t maxIndices = 24'000'000u;
};

/**
 * @brief Versioned little-endian binary format for a cooked mesh.
 *
 * This is the runtime-ready form the cooker writes and the loader reads: the
 * development source format (OBJ/glTF/...) is not shipped. The layout is a
 * fixed-endianness runtime cache, not a portable interchange format; encoding
 * is deterministic so re-cooking identical `MeshData` yields byte-identical
 * output (a prerequisite for content hashing and incremental cook).
 *
 * A vertex flag byte records which optional streams (normals, UVs, skinning)
 * are present, and indices are stored as either a 16-bit or 32-bit buffer
 * chosen from the source `MeshData`. Decoding validates every count and index
 * against the buffer and the supplied limits before allocating, so a corrupt or
 * hostile blob is rejected rather than trusted.
 */
namespace CookedMesh {

/** Encodes `mesh` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const MeshData& mesh);

/**
 * Decodes a cooked mesh blob. Returns nullopt on bad magic/version, truncation,
 * trailing bytes, a stream length that disagrees with the vertex count, an
 * out-of-range index, or a count above `limits`.
 */
std::optional<MeshData> Decode(const std::uint8_t* data, std::size_t size,
                               const CookedMeshLimits& limits = {});

} // namespace CookedMesh

} // namespace Concord::Asset

#endif // CONCORD_COOKEDMESH_H
