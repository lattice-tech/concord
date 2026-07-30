#ifndef CONCORD_DYNAMICAABBTREE_H
#define CONCORD_DYNAMICAABBTREE_H

#include "engine/collision/Aabb.h"
#include "engine/collision/AabbOps.h"
#include "engine/collision/query/Ray.h"
#include "engine/spatial/SpatialId.h"
#include "engine/spatial/SpatialProxy.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace Concord::Spatial {

/**
 * @brief Persistent Dynamic AABB Tree (BVH) over SpatialProxy leaves.
 *
 * Insert/Move/Remove keep a balanced binary tree of fat AABBs. Queries walk
 * only the branches that overlap the probe, so large sparse worlds stay cheap
 * compared to a linear scan. All public APIs are generation-safe: a removed
 * proxy's SpatialId never resolves again even if its slot is reused.
 *
 * The tree is pure CPU data with no Scene/render dependency (AGENTS §5), so it
 * can be unit-tested against a brute-force oracle and later wired into Scene
 * broadphase or visibility extraction without pulling bgfx.
 */
class DynamicAabbTree {
public:
    explicit DynamicAabbTree(float fattenMargin = 0.1f);

    /**
     * Inserts a proxy. Rejects invalid bounds.
     * @return A live SpatialId, or kInvalidSpatialId on failure.
     */
    SpatialId Insert(const SpatialProxy& proxy);

    /**
     * Updates the fat AABB of an existing proxy. If the new bounds still fit
     * inside the fat box the tree topology is left alone; otherwise the leaf is
     * reinserted. Returns false for a stale id or invalid bounds.
     */
    bool Move(SpatialId id, const Collision::Aabb& bounds);

    /** Removes a proxy. Returns false when @p id is already stale. */
    bool Remove(SpatialId id);

    /** Live proxy count (leaf nodes only). */
    std::size_t Size() const noexcept { return m_proxyCount; }

    /** True while @p id names a live leaf. */
    bool IsAlive(SpatialId id) const noexcept;

    /** Resolved proxy, or nullptr when @p id is stale. */
    const SpatialProxy* Find(SpatialId id) const noexcept;

    /**
     * Overwrites the leaf's userData and layer without touching topology.
     * Used by persistent broadphase to re-encode dense frame indices after Move.
     * @return false when @p id is stale.
     */
    bool SetProxyMeta(SpatialId id, std::uint64_t userData, std::uint32_t layer);

    /**
     * Axis-aligned union of every live fat AABB. Returns false when the tree is
     * empty (outBounds left unchanged).
     */
    bool WorldBounds(Collision::Aabb& outBounds) const noexcept;

    /**
     * Calls @p visitor(id, proxy) for every leaf whose fat AABB overlaps
     * @p query and passes @p filter. Visitor may not mutate the tree.
     */
    void QueryOverlap(const Collision::Aabb& query, SpatialQueryFilter filter,
                      const std::function<void(SpatialId, const SpatialProxy&)>& visitor) const;

    /**
     * Closest leaf hit by a world ray under @p filter. Writes the proxy and the
     * entry distance (world units) when true.
     */
    bool QueryRayClosest(const Collision::Ray& ray, float maxDistance,
                         SpatialQueryFilter filter, SpatialId& outId,
                         const SpatialProxy*& outProxy, float& outDistance) const;

    /**
     * Leaves whose fat AABB would be hit by a solid of half-extents @p radius
     * swept from @p origin along a normalized direction up to @p maxDistance.
     */
    void QuerySweep(const Vector3& origin, const Vector3& direction, float radius,
                    float maxDistance, SpatialQueryFilter filter,
                    const std::function<void(SpatialId, const SpatialProxy&, float)>& visitor) const;

    /**
     * Walks the tree against a frustum predicate. @p intersects returns true when
     * a fat AABB may contain visible leaves; internal nodes that fail are pruned.
     * @p nodesVisited counts every node whose bounds were tested.
     */
    void QueryFrustum(
        const std::function<bool(const Collision::Aabb&)>& intersects,
        SpatialQueryFilter filter, std::uint32_t& nodesVisited,
        const std::function<void(SpatialId, const SpatialProxy&)>& visitor) const;

    float FattenMargin() const noexcept { return m_fattenMargin; }

private:
    static constexpr std::uint32_t kNull = std::numeric_limits<std::uint32_t>::max();

    struct Node {
        Collision::Aabb bounds{};
        SpatialProxy proxy{};
        std::uint32_t parent = kNull;
        std::uint32_t child0 = kNull;
        std::uint32_t child1 = kNull;
        std::uint32_t height = 0;
        std::uint32_t generation = 0;
        bool leaf = false;
        bool occupied = false;
    };

    std::uint32_t AllocateNode();
    void FreeNode(std::uint32_t index);
    bool ResolveLeaf(SpatialId id, std::uint32_t& outIndex) const noexcept;
    std::uint32_t InsertLeaf(std::uint32_t leaf);
    void RemoveLeaf(std::uint32_t leaf);
    std::uint32_t Balance(std::uint32_t index);
    void Refit(std::uint32_t index);
    std::uint32_t SiblingCostPick(std::uint32_t leaf, std::uint32_t candidate) const;

    float m_fattenMargin = 0.1f;
    std::vector<Node> m_nodes;
    std::vector<std::uint32_t> m_free;
    std::uint32_t m_root = kNull;
    std::size_t m_proxyCount = 0;
};

} // namespace Concord::Spatial

#endif // CONCORD_DYNAMICAABBTREE_H
