#include "engine/render/batch/RenderBatcher.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <numeric>
#include <utility>

namespace Concord {

namespace {

/**
 * Hashes one field into an FNV-1a accumulator (mix + multiply step).
 *
 * Used by HashMaterial below to fold each RenderMaterial field's bit pattern
 * into a running 64-bit value. Templated so the call site can pass either
 * an integral/enum field or a float through std::bit_cast<uint32_t>.
 */
constexpr std::uint64_t MixFnv(std::uint64_t hash, std::uint64_t value) noexcept
{
    hash ^= value;
    hash *= 0x100000001b3ULL; // FNV-1a 64-bit prime
    return hash;
}

} // namespace

std::uint64_t HashMaterial(const RenderMaterial& material) noexcept
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;

    std::uint64_t hash = kFnvOffsetBasis;

    hash = MixFnv(hash, material.albedo);
    hash = MixFnv(hash, material.gradientTo);
    hash = MixFnv(hash, material.emissive);

    // Fold floats by their bit representation. std::bit_cast aliases the
    // raw bytes as a fixed-width integer without UB; NaN and mixed signed
    // zeros would hash as their bit pattern too, but ResolveMaterial never
    // yields either, so every value is canonical.
    std::uint32_t bits = 0;
    bits = std::bit_cast<std::uint32_t>(material.metallic);
    hash = MixFnv(hash, bits);
    bits = std::bit_cast<std::uint32_t>(material.roughness);
    hash = MixFnv(hash, bits);
    bits = std::bit_cast<std::uint32_t>(material.reflectivity);
    hash = MixFnv(hash, bits);
    bits = std::bit_cast<std::uint32_t>(material.emissiveStrength);
    hash = MixFnv(hash, bits);

    hash = MixFnv(hash, material.lit ? 1u : 0u);
    hash = MixFnv(hash, material.gradient ? 1u : 0u);
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.gradientAxis));

    hash = MixFnv(hash, static_cast<std::uint64_t>(material.albedoMap));
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.normalMap));
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.metallicRoughnessMap));
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.emissiveMap));

    hash = MixFnv(hash, static_cast<std::uint64_t>(material.depthTest));
    hash = MixFnv(hash, material.depthWrite ? 1u : 0u);
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.cull));
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.blend));
    hash = MixFnv(hash, static_cast<std::uint64_t>(material.priority));

    return hash;
}

void RenderBatcher::BeginFrame()
{
    m_index.clear();
    // Return each slot's per-key command vector to the pool for reuse, so
    // the next frame's Add() borrows an already-allocated vector instead of
    // growing a fresh heap allocation per key.
    for (Slot& slot : m_slots) {
        slot.commands.clear();
        m_pool.push_back(std::move(slot.commands));
    }
    m_slots.clear();
    m_batches.clear();
    m_commandPointers.clear();
}

void RenderBatcher::Add(const MeshDrawCommand& command)
{
    const BatchKey key{
        PackMesh(command.mesh), HashMaterial(command.material), command.effect, command.rayTraced};

    auto it = m_index.find(key);
    if (it != m_index.end()) {
        // Existing hash bucket. The hash is only a *candidate* — confirm by
        // operator== on the full material so two materials that folded to the
        // same hash never merge. Slot count under one key is typically one,
        // so this scan is a single comparison in the common case.
        const std::uint32_t firstSlot = it->second;
        if (m_slots[firstSlot].batch.material == command.material) {
            m_slots[firstSlot].commands.push_back(&command);
            return;
        }
        for (std::uint32_t slotIdx = firstSlot + 1; slotIdx < m_slots.size(); ++slotIdx) {
            const Slot& candidate = m_slots[slotIdx];
            if (PackMesh(candidate.batch.mesh) == key.mesh
                && candidate.materialHash == key.material
                && candidate.batch.effect == key.effect
                && candidate.batch.realtimeReflection == key.realtimeReflection
                && candidate.batch.material == command.material) {
                m_slots[slotIdx].commands.push_back(&command);
                return;
            }
        }
        // Genuine hash collision: a different material shared this key.
        // Fall through and start a fresh slot under the same key chain so
        // each distinct material keeps its own batch instead of blending.
    }

    // New (mesh, material) pair — recycle a vector from the pool if one is
    // ready, else grow one. Keeps the per-frame grouping allocation-free.
    std::vector<const MeshDrawCommand*> recycled;
    if (!m_pool.empty()) {
        recycled = std::move(m_pool.back());
        m_pool.pop_back();
    }
    recycled.push_back(&command);

    const std::uint32_t slotIndex = static_cast<std::uint32_t>(m_slots.size());

    Slot slot;
    slot.batch.mesh = command.mesh;
    slot.batch.material = command.material;
    slot.batch.effect = command.effect;
    slot.batch.realtimeReflection = command.rayTraced;
    slot.materialHash = key.material;
    slot.commands = std::move(recycled);
    m_slots.push_back(std::move(slot));

    m_index.try_emplace(key, slotIndex);
}

void RenderBatcher::Finish()
{
    if (m_slots.empty()) {
        m_batches.clear();
        m_commandPointers.clear();
        return;
    }

    // Build the deterministic sort order: original slot indices sorted by
    // (priority, mesh index, material hash) ascending. Sorting indices instead
    // of moving the slots themselves keeps the slot shuffle allocation-free
    // and lets us build the flattened command-pointer array in one pass.
    m_order.resize(m_slots.size());
    std::iota(m_order.begin(), m_order.end(), 0u);
    std::sort(m_order.begin(), m_order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const RenderBatch& ba = m_slots[a].batch;
        const RenderBatch& bb = m_slots[b].batch;

        // Opaque geometry always renders before any transparent (Alpha/
        // Additive) batch: blended draws read the opaque depth buffer but do
        // not write it, so they must be issued after every opaque surface has
        // populated depth. This dominates the user's priority knob because
        // drawing a glow before the wall behind it would let the wall's later
        // opaque write clobber the accumulated light.
        const bool aBlend = ba.material.blend != Material::BlendMode::Opaque;
        const bool bBlend = bb.material.blend != Material::BlendMode::Opaque;
        if (aBlend != bBlend) {
            return !aBlend; // opaque (false) sorts before transparent (true)
        }

        if (ba.material.priority != bb.material.priority) {
            return ba.material.priority < bb.material.priority;
        }
        if (ba.mesh.index != bb.mesh.index) {
            return ba.mesh.index < bb.mesh.index;
        }
        if (ba.effect != bb.effect) {
            return ba.effect < bb.effect;
        }
        if (ba.realtimeReflection != bb.realtimeReflection) {
            return !ba.realtimeReflection;
        }
        // Tie-break by material hash so two batches with the same priority
        // and mesh (different materials, valid case) keep a stable order
        // independent of insertion/hashing noise.
        return m_slots[a].materialHash < m_slots[b].materialHash;
    });

    // Count total commands once so the flattened array never reallocates
    // mid-build (otherwise spans built on early references would dangle).
    std::size_t total = 0;
    for (const Slot& slot : m_slots) {
        total += slot.commands.size();
    }
    m_commandPointers.clear();
    m_commandPointers.reserve(total);

    m_ranges.clear();
    m_ranges.reserve(m_order.size());

    for (std::uint32_t slotIndex : m_order) {
        const std::vector<const MeshDrawCommand*>& cmds = m_slots[slotIndex].commands;
        const std::size_t first = m_commandPointers.size();
        m_commandPointers.insert(m_commandPointers.end(), cmds.begin(), cmds.end());
        m_ranges.push_back(BatchRange{ slotIndex, first, cmds.size() });
    }

    // Build the published batch list pointing into m_commandPointers now
    // that the flattened array is final and stable.
    m_batches.clear();
    m_batches.reserve(m_order.size());
    for (const BatchRange& r : m_ranges) {
        m_slots[r.slotIndex].batch.commands = std::span<const MeshDrawCommand* const>(
            m_commandPointers.data() + r.first, r.count);
        m_batches.push_back(m_slots[r.slotIndex].batch);
    }
}

} // namespace Concord
