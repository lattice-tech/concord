#include "engine/spatial/DynamicAabbTree.h"

#include "engine/collision/query/RayIntersection.h"

#include <vector>

namespace Concord::Spatial {
namespace {

using Collision::Aabb;
using Collision::IntersectRayAabb;
using Collision::IntersectSweepAabb;
using Collision::IsValidAabb;
using Collision::NormalizeRay;

bool PassesFilter(const SpatialProxy& proxy, SpatialQueryFilter filter) noexcept
{
    return (proxy.layer & filter.layerMask) != 0u;
}

} // namespace

void DynamicAabbTree::QueryOverlap(
    const Aabb& query, SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor) const
{
    if (m_root == kNull || !visitor || !IsValidAabb(query)) {
        return;
    }
    std::vector<std::uint32_t> stack;
    stack.push_back(m_root);
    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        const Node& node = m_nodes[index];
        if (!node.bounds.Overlaps(query)) {
            continue;
        }
        if (node.leaf) {
            if (PassesFilter(node.proxy, filter) && node.proxy.bounds.Overlaps(query)) {
                visitor(SpatialId{index, node.generation}, node.proxy);
            }
            continue;
        }
        stack.push_back(node.child0);
        stack.push_back(node.child1);
    }
}

bool DynamicAabbTree::QueryRayClosest(const Collision::Ray& ray, float maxDistance,
                                      SpatialQueryFilter filter, SpatialId& outId,
                                      const SpatialProxy*& outProxy,
                                      float& outDistance) const
{
    outProxy = nullptr;
    Collision::Ray normalized{};
    if (m_root == kNull || !NormalizeRay(ray, normalized) || maxDistance < 0.0f) {
        return false;
    }
    float best = maxDistance;
    bool hit = false;
    std::vector<std::uint32_t> stack;
    stack.push_back(m_root);
    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        const Node& node = m_nodes[index];
        float tNode = 0.0f;
        if (!IntersectRayAabb(normalized, node.bounds, 0.0f, best, tNode)) {
            continue;
        }
        if (node.leaf) {
            float tLeaf = 0.0f;
            if (PassesFilter(node.proxy, filter)
                && IntersectRayAabb(normalized, node.proxy.bounds, 0.0f, best, tLeaf)
                && tLeaf <= best) {
                best = tLeaf;
                outId = SpatialId{index, node.generation};
                outProxy = &node.proxy;
                outDistance = tLeaf;
                hit = true;
            }
            continue;
        }
        stack.push_back(node.child0);
        stack.push_back(node.child1);
    }
    return hit;
}

void DynamicAabbTree::QuerySweep(
    const Vector3& origin, const Vector3& direction, float radius, float maxDistance,
    SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&, float)>& visitor) const
{
    if (m_root == kNull || !visitor || maxDistance < 0.0f) {
        return;
    }
    Collision::Ray ray{origin, direction};
    Collision::Ray normalized{};
    if (!NormalizeRay(ray, normalized)) {
        return;
    }
    std::vector<std::uint32_t> stack;
    stack.push_back(m_root);
    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        const Node& node = m_nodes[index];
        float tNode = 0.0f;
        if (!IntersectSweepAabb(normalized.origin, normalized.direction, radius,
                                maxDistance, node.bounds, tNode)) {
            continue;
        }
        if (node.leaf) {
            float tLeaf = 0.0f;
            if (PassesFilter(node.proxy, filter)
                && IntersectSweepAabb(normalized.origin, normalized.direction, radius,
                                      maxDistance, node.proxy.bounds, tLeaf)) {
                visitor(SpatialId{index, node.generation}, node.proxy, tLeaf);
            }
            continue;
        }
        stack.push_back(node.child0);
        stack.push_back(node.child1);
    }
}

void DynamicAabbTree::QueryFrustum(
    const std::function<bool(const Collision::Aabb&)>& intersects,
    SpatialQueryFilter filter, std::uint32_t& nodesVisited,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor) const
{
    nodesVisited = 0;
    if (m_root == kNull || !intersects || !visitor) {
        return;
    }
    std::vector<std::uint32_t> stack;
    stack.push_back(m_root);
    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        const Node& node = m_nodes[index];
        ++nodesVisited;
        if (!intersects(node.bounds)) {
            continue;
        }
        if (node.leaf) {
            if (PassesFilter(node.proxy, filter) && intersects(node.proxy.bounds)) {
                visitor(SpatialId{index, node.generation}, node.proxy);
            }
            continue;
        }
        stack.push_back(node.child0);
        stack.push_back(node.child1);
    }
}

} // namespace Concord::Spatial
