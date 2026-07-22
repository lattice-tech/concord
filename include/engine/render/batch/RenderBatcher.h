#ifndef CONCORD_RENDERBATCHER_H
#define CONCORD_RENDERBATCHER_H

#include "engine/render/batch/BatchKey.h"
#include "engine/render/batch/MaterialHash.h"
#include "engine/render/batch/RenderBatch.h"
#include "engine/render/backend/IRenderBackend.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Concord {

/**
 * Groups one frame's MeshDrawCommands into the smallest number of instanced
 * submits the backend can issue.
 *
 * The render thread's job each frame: it receives a flat list of "draw this
 * mesh, here, with this material" commands (SubmitMesh), then RenderView
 * must turn that into GPU calls. Without batching, that is one submit per
 * draw; with batching, draws that share both a mesh buffer pair and a fully
 * equal resolved material collapse into one instanced submit, so the
 * per-command overhead stays on the CPU and the GPU pays one
 * draw call per (mesh, material) pair instead of one per object.
 *
 * This class is intentionally *backend-agnostic*: it only rearranges the
 * command pointers and produces RenderBatches. The backend resolves the
 * mesh handle to a GPU buffer pair itself (only it can), so a stale
 * handle is reported back as a null pointer lookup after Finish, and the
 * backend skips that batch rather than corrupting the instancing buffer.
 *
 * Memory is reused across frames: the per-key map, the batch list, and the
 * flattened command-pointer array all `clear()` to keep their capacity, so a
 * steady scene produces no heap allocations after its first frame. A small
 * pool of per-key command vectors is also recycled so the per-frame churn of
 * "build N key buckets, then drop them" stays allocation-free.
 *
 * Lifecycle per frame:
 *   1. BeginFrame()  — drop the previous frame's grouping, keep all capacity.
 *   2. Add(cmd)      — once per MeshDrawCommand submitted this frame.
 *   3. Finish()      — sort batches deterministically and flatten commands.
 *   4. Batches()     — read by the backend to issue instanced submits.
 *
 * Finish produces a deterministic order: batches are sorted by
 * (DrawOptions::priority, mesh index, material hash) ascending. The
 * priority realizes Material::DrawOptions's "lower draws first, higher on
 * top" rule; the secondary keys tie-break purely from the data so the same
 * scene renders in the same order every run, which makes regressions
 * reproducible and lets frame diffs catch reordering bugs (a non-stable
 * order would make every frame look "changed").
 */
class RenderBatcher {
public:
    /**
     * Drops the previous frame's grouping without freeing capacity.
     *
     * All per-key state is cleared; the batch list, the flattened command
     * pointer array and the recycled command-vector pool keep their backing
     * storage so the next frame's Add/Finish allocations reuse them.
     */
    void BeginFrame();

    /**
     * Appends one MeshDrawCommand to this frame's batch build.
     *
     * `command` must outlive the call to Finish (the batcher stores a
     * pointer to it), which is guaranteed today because the backend keeps
     * the per-view pending queue alive across SubmitMesh and RenderView.
     */
    void Add(const MeshDrawCommand& command);

    /**
     * Finalizes the frame's batches: groups the accumulated commands by
     * (mesh, material), orders them deterministically and flattens the
     * per-batch command pointers into one contiguous span-backed array.
     * After this, Batches() returns the ready-to-consume list.
     */
    void Finish();

    /**
     * The finalized batches for this frame; empty before Finish.
     *
     * The returned span and each batch's `commands` span stay valid until
     * the next BeginFrame (or this batcher's destruction).
     */
    std::span<const RenderBatch> Batches() const noexcept { return m_batches; }

private:
    struct BatchRange {
        std::uint32_t slotIndex = 0;
        std::size_t first = 0;
        std::size_t count = 0;
    };

    /**
     * One accumulator per distinct (mesh, material) key during Add.
     *
     * `commands` grows across Add calls; `batch.commands` is the empty span
     * until Finish rewrites it to point into m_commandPointers.
     */
    struct Slot {
        RenderBatch batch{};
        std::uint64_t materialHash = 0;
        std::vector<const MeshDrawCommand*> commands;
    };

    /**
     * Maps a (mesh, material-hash) key to the slots that share it.
     *
     * The value is a small list of slot indices rather than a single one
     * because the key carries a *hash* of the material, not the material
     * itself: two materials that folded to the same hash chain under this
     * entry, and Add confirms them with operator== before merging. In the
     * common (collision-free) case each value list holds exactly one index
     * and the lookup stays O(1); the rare collision case falls back to a
     * short linear scan over a handful of slots.
     */
    std::unordered_map<BatchKey, std::uint32_t, BatchKeyHash> m_index;

    /** One Slot per distinct (mesh, material) key seen this frame. */
    std::vector<Slot> m_slots;

    /**
     * Flattened command pointers in batch-major order, owned here so the
     * spans handed out from Finish point at stable storage.
     */
    std::vector<const MeshDrawCommand*> m_commandPointers;

    /**
     * Finalized batch list published to the backend. Written in Finish from
     * m_slots and kept here for Batches() to return without per-frame
     * reallocation.
     */
    std::vector<RenderBatch> m_batches;

    /** Reused sorting and span-building storage used by Finish. */
    std::vector<std::uint32_t> m_order;
    std::vector<BatchRange> m_ranges;

    /**
     * Pool of recycled per-key command vectors: each frame's slots borrow a
     * vector from here on Add and return it to the pool on BeginFrame.
     * Keeps the per-frame grouping allocation-free.
     */
    std::vector<std::vector<const MeshDrawCommand*>> m_pool;
};

} // namespace Concord

#endif // CONCORD_RENDERBATCHER_H
