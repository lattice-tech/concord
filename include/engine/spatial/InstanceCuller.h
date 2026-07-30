#ifndef CONCORD_INSTANCECULLER_H
#define CONCORD_INSTANCECULLER_H

#include "engine/render/frame/RenderInstance.h"
#include "engine/spatial/Frustum.h"
#include "engine/spatial/VisibilityStats.h"
#include "engine/task/TaskGraph.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Concord::Spatial {

/** Instances per cull chunk when the parallel path is taken. */
inline constexpr std::size_t kInstanceCullChunkSize = 1024;

/** Authored instance count at which chunked culling starts paying off. */
inline constexpr std::size_t kInstanceCullParallelThreshold = 2048;

/** Executes a CPU task graph and returns its timings (see EngineLoop::RunTaskGraph). */
using TaskGraphRunner = std::function<TaskGraphStats(TaskGraph)>;

/**
 * @brief Frustum-culls authored draws through one temporary DynamicAabbTree.
 *
 * Each instance is bounded by its own model-space box when it carries one and
 * by the unit cube otherwise (see WorldAabbForInstance). Survivors are *moved*
 * out of @p authored into @p outSubmitted in ascending authored order, so the
 * relative draw order the scene produced is preserved and the result never
 * depends on tree topology. Instances whose world matrix is not finite yield an
 * invalid box and are dropped.
 *
 * @param authored Draws collected this frame; surviving entries are moved out.
 *        Rejected entries are left readable so shadow-caster selection can still
 *        use them.
 * @param outSubmitted Cleared, then filled with the visible draws.
 * @param outCulledIndices Optional; receives the rejected authored indices in
 *        ascending order.
 * @return Counters for this pass (authored/extracted/culled/submitted/nodesVisited).
 */
VisibilityStats CullInstances(std::vector<RenderInstance>& authored,
                              const Frustum& frustum,
                              std::vector<RenderInstance>& outSubmitted,
                              std::vector<std::size_t>* outCulledIndices = nullptr);

/**
 * @brief Chunked CullInstances: one worker task per @p chunkSize instances.
 *
 * Every chunk builds and walks its own tree over a disjoint index range, so the
 * visible set and its order are bit-identical to CullInstances regardless of how
 * the workers interleave. Falls back to the serial path when the work is too
 * small to split or @p runner is empty.
 *
 * `nodesVisited` sums the per-chunk walks, so it is comparable across frames but
 * not to the single-tree number for the same scene.
 *
 * @param chunkSize Instances per task; 0 or >= authored.size() runs serially.
 * @param runner Executes the cull graph (must block until every task is done).
 * @param outTaskStats Receives the graph timings; left untouched on the serial path.
 * @param outCulledIndices Optional; receives the rejected authored indices in
 *        ascending order, which shadow-caster selection needs (see
 *        ShadowCasterCuller) because a caster outside the view still darkens it.
 */
VisibilityStats CullInstancesParallel(std::vector<RenderInstance>& authored,
                                      const Frustum& frustum,
                                      std::size_t chunkSize,
                                      const TaskGraphRunner& runner,
                                      std::vector<RenderInstance>& outSubmitted,
                                      TaskGraphStats& outTaskStats,
                                      std::vector<std::size_t>* outCulledIndices = nullptr);

} // namespace Concord::Spatial

#endif // CONCORD_INSTANCECULLER_H
