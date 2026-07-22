#ifndef CONCORD_MATERIALHASH_H
#define CONCORD_MATERIALHASH_H

#include "engine/render/material/RenderMaterial.h"

#include <cstdint>

namespace Concord {

/**
 * A 64-bit FNV-1a hash folded over every field of a resolved material.
 *
 * The batcher (see engine/render/batch/RenderBatcher) groups draws whose
 * materials set identical shading state into one instanced submit. It
 * identifies "identical" by first hashing the material into this small key
 * and only doing a full operator== on hash collisions — so a hash that
 * distributes well keeps the collision check rare.
 *
 * The hash is over raw bit patterns:
 *   - Integer and enum fields are folded in by their in-memory value.
 *   - Float fields are folded by their bit representation (`std::uint32_t`
 *     alias of the float's bytes via memcpy), not their numeric value. That
 *     means -0.0f and +0.0f hash differently; this is deliberate, because
 *     ResolveMaterial never yields either signed-zero edge, so the bit
 *     pattern it sees is canonical and the hash stays stable across runs.
 *
 * The hash is platform-independent for any little-endian target the engine
 * supports today (the POD layout is fixed); big-endian ports would rehash
 * field bytes in reverse order, which is fine since two equal materials
 * always hash equal within one process.
 */
std::uint64_t HashMaterial(const RenderMaterial& material) noexcept;

} // namespace Concord

#endif // CONCORD_MATERIALHASH_H