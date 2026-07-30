#include "engine/spatial/InstanceCuller.h"

#include "engine/spatial/DynamicAabbTree.h"
#include "engine/spatial/SpatialProxy.h"
#include "engine/spatial/WorldAabbFromMatrix.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Concord::Spatial {
namespace {

/**
 * Culls the half-open authored range [begin, end) with a local tree.
 *
 * Survivors are written as ascending authored indices; the sort is what makes
 * the result independent of insertion/traversal order and therefore identical
 * between the serial and chunked paths.
 */
void CullRange(const std::vector<RenderInstance>& authored, std::size_t begin,
               std::size_t end, const Frustum& frustum,
               std::vector<std::size_t>& outVisible, std::uint32_t& outNodesVisited)
{
    outVisible.clear();
    outNodesVisited = 0;
    if (begin >= end || end > authored.size()) {
        return;
    }

    DynamicAabbTree tree(0.0f);
    for (std::size_t i = begin; i < end; ++i) {
        const RenderInstance& instance = authored[i];
        SpatialProxy proxy;
        proxy.bounds = WorldAabbForInstance(instance.world, instance.hasLocalBounds,
                                           instance.localMin, instance.localMax);
        proxy.userData = static_cast<std::uint64_t>(i);
        proxy.layer = 1u;
        tree.Insert(proxy);
    }

    outVisible.reserve(end - begin);
    tree.QueryFrustum(
        [&frustum](const Collision::Aabb& bounds) {
            return FrustumIntersectsAabb(frustum, bounds);
        },
        {}, outNodesVisited,
        [&outVisible](SpatialId, const SpatialProxy& proxy) {
            outVisible.push_back(static_cast<std::size_t>(proxy.userData));
        });
    std::sort(outVisible.begin(), outVisible.end());
}

/**
 * Appends the complement of @p visible over [begin, end) to @p out.
 * @p visible must be ascending, which CullRange guarantees.
 */
void AppendCulled(std::size_t begin, std::size_t end,
                  const std::vector<std::size_t>& visible,
                  std::vector<std::size_t>* out)
{
    if (out == nullptr) {
        return;
    }
    std::size_t cursor = 0;
    for (std::size_t index = begin; index < end; ++index) {
        if (cursor < visible.size() && visible[cursor] == index) {
            ++cursor;
            continue;
        }
        out->push_back(index);
    }
}

/** Moves the instances named by @p visible out of @p authored, in given order. */
void EmitVisible(std::vector<RenderInstance>& authored,
                 const std::vector<std::size_t>& visible,
                 std::vector<RenderInstance>& outSubmitted)
{
    for (std::size_t index : visible) {
        if (index < authored.size()) {
            outSubmitted.push_back(std::move(authored[index]));
        }
    }
}

VisibilityStats FinishStats(std::size_t authoredCount, std::uint32_t nodesVisited,
                            const std::vector<RenderInstance>& submitted) noexcept
{
    VisibilityStats stats;
    stats.authored = static_cast<std::uint32_t>(authoredCount);
    stats.extracted = stats.authored;
    stats.submitted = static_cast<std::uint32_t>(submitted.size());
    stats.culled = stats.authored - stats.submitted;
    stats.nodesVisited = nodesVisited;
    return stats;
}

} // namespace

VisibilityStats CullInstances(std::vector<RenderInstance>& authored,
                              const Frustum& frustum,
                              std::vector<RenderInstance>& outSubmitted,
                              std::vector<std::size_t>* outCulledIndices)
{
    outSubmitted.clear();
    outSubmitted.reserve(authored.size());
    if (outCulledIndices != nullptr) {
        outCulledIndices->clear();
    }

    std::vector<std::size_t> visible;
    std::uint32_t nodesVisited = 0;
    CullRange(authored, 0, authored.size(), frustum, visible, nodesVisited);
    AppendCulled(0, authored.size(), visible, outCulledIndices);
    EmitVisible(authored, visible, outSubmitted);
    return FinishStats(authored.size(), nodesVisited, outSubmitted);
}

VisibilityStats CullInstancesParallel(std::vector<RenderInstance>& authored,
                                      const Frustum& frustum,
                                      std::size_t chunkSize,
                                      const TaskGraphRunner& runner,
                                      std::vector<RenderInstance>& outSubmitted,
                                      TaskGraphStats& outTaskStats,
                                      std::vector<std::size_t>* outCulledIndices)
{
    const std::size_t count = authored.size();
    if (!runner || chunkSize == 0 || count <= chunkSize) {
        return CullInstances(authored, frustum, outSubmitted, outCulledIndices);
    }

    const std::size_t chunks = (count + chunkSize - 1) / chunkSize;
    std::vector<std::vector<std::size_t>> visible(chunks);
    std::vector<std::uint32_t> nodesVisited(chunks, 0);

    TaskGraph graph;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const std::size_t begin = chunk * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, count);
        graph.Add("Scene.VisibilityCull." + std::to_string(chunk),
                  [&authored, &frustum, &visible, &nodesVisited, chunk, begin, end] {
                      CullRange(authored, begin, end, frustum, visible[chunk],
                                nodesVisited[chunk]);
                  });
    }
    outTaskStats = runner(std::move(graph));

    outSubmitted.clear();
    outSubmitted.reserve(count);
    if (outCulledIndices != nullptr) {
        outCulledIndices->clear();
    }
    std::uint32_t visitedTotal = 0;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        visitedTotal += nodesVisited[chunk];
        const std::size_t begin = chunk * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, count);
        AppendCulled(begin, end, visible[chunk], outCulledIndices);
        EmitVisible(authored, visible[chunk], outSubmitted);
    }
    return FinishStats(count, visitedTotal, outSubmitted);
}

} // namespace Concord::Spatial
