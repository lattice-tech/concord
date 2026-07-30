#include "engine/spatial/DynamicAabbTree.h"

#include <algorithm>
#include <utility>

namespace Concord::Spatial {
namespace {

using Collision::Aabb;
using Collision::AabbContainsAabb;
using Collision::AabbSurfaceArea;
using Collision::FattenAabb;
using Collision::IsValidAabb;
using Collision::UnionAabb;

} // namespace

DynamicAabbTree::DynamicAabbTree(float fattenMargin)
    : m_fattenMargin(fattenMargin > 0.0f ? fattenMargin : 0.0f)
{
}

std::uint32_t DynamicAabbTree::AllocateNode()
{
    if (!m_free.empty()) {
        const std::uint32_t index = m_free.back();
        m_free.pop_back();
        Node& node = m_nodes[index];
        const std::uint32_t generation = node.generation;
        node = Node{};
        node.generation = generation;
        node.occupied = true;
        return index;
    }
    const std::uint32_t index = static_cast<std::uint32_t>(m_nodes.size());
    Node node{};
    node.occupied = true;
    node.generation = 1;
    m_nodes.push_back(node);
    return index;
}

void DynamicAabbTree::FreeNode(std::uint32_t index)
{
    Node& node = m_nodes[index];
    node.occupied = false;
    node.leaf = false;
    node.parent = kNull;
    node.child0 = kNull;
    node.child1 = kNull;
    node.height = 0;
    node.proxy = {};
    ++node.generation;
    if (node.generation == 0) {
        node.generation = 1;
    }
    m_free.push_back(index);
}

bool DynamicAabbTree::ResolveLeaf(SpatialId id, std::uint32_t& outIndex) const noexcept
{
    if (!id.IsValid() || id.slot >= m_nodes.size()) {
        return false;
    }
    const Node& node = m_nodes[id.slot];
    if (!node.occupied || !node.leaf || node.generation != id.generation) {
        return false;
    }
    outIndex = id.slot;
    return true;
}

SpatialId DynamicAabbTree::Insert(const SpatialProxy& proxy)
{
    if (!IsValidAabb(proxy.bounds)) {
        return kInvalidSpatialId;
    }
    const std::uint32_t leaf = AllocateNode();
    // Never keep a Node& across InsertLeaf: it may AllocateNode and reallocate.
    m_nodes[leaf].leaf = true;
    m_nodes[leaf].proxy = proxy;
    m_nodes[leaf].bounds = FattenAabb(proxy.bounds, m_fattenMargin);
    m_nodes[leaf].height = 0;
    m_root = InsertLeaf(leaf);
    ++m_proxyCount;
    return SpatialId{leaf, m_nodes[leaf].generation};
}

bool DynamicAabbTree::Move(SpatialId id, const Aabb& bounds)
{
    std::uint32_t leaf = kNull;
    if (!ResolveLeaf(id, leaf) || !IsValidAabb(bounds)) {
        return false;
    }
    Node& node = m_nodes[leaf];
    node.proxy.bounds = bounds;
    if (AabbContainsAabb(node.bounds, bounds)) {
        return true;
    }
    RemoveLeaf(leaf);
    node.bounds = FattenAabb(bounds, m_fattenMargin);
    m_root = InsertLeaf(leaf);
    return true;
}

bool DynamicAabbTree::Remove(SpatialId id)
{
    std::uint32_t leaf = kNull;
    if (!ResolveLeaf(id, leaf)) {
        return false;
    }
    RemoveLeaf(leaf);
    FreeNode(leaf);
    --m_proxyCount;
    return true;
}

bool DynamicAabbTree::IsAlive(SpatialId id) const noexcept
{
    std::uint32_t leaf = kNull;
    return ResolveLeaf(id, leaf);
}

const SpatialProxy* DynamicAabbTree::Find(SpatialId id) const noexcept
{
    std::uint32_t leaf = kNull;
    if (!ResolveLeaf(id, leaf)) {
        return nullptr;
    }
    return &m_nodes[leaf].proxy;
}

bool DynamicAabbTree::SetProxyMeta(SpatialId id, std::uint64_t userData,
                                   std::uint32_t layer)
{
    std::uint32_t leaf = kNull;
    if (!ResolveLeaf(id, leaf)) {
        return false;
    }
    m_nodes[leaf].proxy.userData = userData;
    m_nodes[leaf].proxy.layer = layer == 0 ? 1u : layer;
    return true;
}

bool DynamicAabbTree::WorldBounds(Aabb& outBounds) const noexcept
{
    if (m_root == kNull) {
        return false;
    }
    outBounds = m_nodes[m_root].bounds;
    return true;
}

std::uint32_t DynamicAabbTree::InsertLeaf(std::uint32_t leaf)
{
    if (m_root == kNull) {
        m_nodes[leaf].parent = kNull;
        return leaf;
    }

    std::uint32_t sibling = SiblingCostPick(leaf, m_root);
    const std::uint32_t oldParent = m_nodes[sibling].parent;
    const std::uint32_t newParent = AllocateNode();
    m_nodes[newParent].occupied = true;
    m_nodes[newParent].leaf = false;
    m_nodes[newParent].parent = oldParent;
    m_nodes[newParent].child0 = sibling;
    m_nodes[newParent].child1 = leaf;
    m_nodes[newParent].bounds =
        UnionAabb(m_nodes[leaf].bounds, m_nodes[sibling].bounds);
    m_nodes[newParent].height =
        1u + std::max(m_nodes[sibling].height, m_nodes[leaf].height);
    m_nodes[sibling].parent = newParent;
    m_nodes[leaf].parent = newParent;

    if (oldParent != kNull) {
        if (m_nodes[oldParent].child0 == sibling) {
            m_nodes[oldParent].child0 = newParent;
        } else {
            m_nodes[oldParent].child1 = newParent;
        }
        Refit(oldParent);
        return m_root;
    }
    return newParent;
}

void DynamicAabbTree::RemoveLeaf(std::uint32_t leaf)
{
    if (leaf == m_root) {
        m_root = kNull;
        return;
    }
    const std::uint32_t parent = m_nodes[leaf].parent;
    const std::uint32_t grand = m_nodes[parent].parent;
    const std::uint32_t sibling =
        m_nodes[parent].child0 == leaf ? m_nodes[parent].child1 : m_nodes[parent].child0;
    m_nodes[sibling].parent = grand;
    if (grand != kNull) {
        if (m_nodes[grand].child0 == parent) {
            m_nodes[grand].child0 = sibling;
        } else {
            m_nodes[grand].child1 = sibling;
        }
        FreeNode(parent);
        Refit(grand);
    } else {
        m_root = sibling;
        FreeNode(parent);
    }
}

std::uint32_t DynamicAabbTree::SiblingCostPick(std::uint32_t leaf,
                                              std::uint32_t candidate) const
{
    const Aabb& leafBounds = m_nodes[leaf].bounds;
    while (!m_nodes[candidate].leaf) {
        const std::uint32_t child0 = m_nodes[candidate].child0;
        const std::uint32_t child1 = m_nodes[candidate].child1;
        const float area = AabbSurfaceArea(m_nodes[candidate].bounds);
        const Aabb combined = UnionAabb(m_nodes[candidate].bounds, leafBounds);
        const float combinedArea = AabbSurfaceArea(combined);
        const float costParent = 2.0f * combinedArea;
        const float inheritance = 2.0f * (combinedArea - area);

        auto childCost = [&](std::uint32_t child) {
            const Aabb childCombined = UnionAabb(leafBounds, m_nodes[child].bounds);
            if (m_nodes[child].leaf) {
                return AabbSurfaceArea(childCombined) + inheritance;
            }
            const float oldArea = AabbSurfaceArea(m_nodes[child].bounds);
            return (AabbSurfaceArea(childCombined) - oldArea) + inheritance;
        };
        const float cost0 = childCost(child0);
        const float cost1 = childCost(child1);
        if (costParent < cost0 && costParent < cost1) {
            break;
        }
        candidate = cost0 < cost1 ? child0 : child1;
    }
    return candidate;
}

void DynamicAabbTree::Refit(std::uint32_t index)
{
    while (index != kNull) {
        index = Balance(index);
        Node& node = m_nodes[index];
        const std::uint32_t c0 = node.child0;
        const std::uint32_t c1 = node.child1;
        node.bounds = UnionAabb(m_nodes[c0].bounds, m_nodes[c1].bounds);
        node.height = 1u + std::max(m_nodes[c0].height, m_nodes[c1].height);
        index = node.parent;
    }
}

} // namespace Concord::Spatial
