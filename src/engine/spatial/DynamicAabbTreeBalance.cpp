#include "engine/spatial/DynamicAabbTree.h"

#include "engine/collision/AabbOps.h"

#include <algorithm>

namespace Concord::Spatial {

std::uint32_t DynamicAabbTree::Balance(std::uint32_t iA)
{
    Node& a = m_nodes[iA];
    if (a.leaf || a.height < 2) {
        return iA;
    }
    const std::uint32_t iB = a.child0;
    const std::uint32_t iC = a.child1;
    const int balance = static_cast<int>(m_nodes[iC].height)
        - static_cast<int>(m_nodes[iB].height);
    if (balance > 1) {
        const std::uint32_t iF = m_nodes[iC].child0;
        const std::uint32_t iG = m_nodes[iC].child1;
        m_nodes[iC].child0 = iA;
        m_nodes[iC].parent = a.parent;
        a.parent = iC;
        if (m_nodes[iC].parent != kNull) {
            if (m_nodes[m_nodes[iC].parent].child0 == iA) {
                m_nodes[m_nodes[iC].parent].child0 = iC;
            } else {
                m_nodes[m_nodes[iC].parent].child1 = iC;
            }
        } else {
            m_root = iC;
        }
        if (m_nodes[iF].height > m_nodes[iG].height) {
            m_nodes[iC].child1 = iF;
            a.child1 = iG;
            m_nodes[iG].parent = iA;
        } else {
            m_nodes[iC].child1 = iG;
            a.child1 = iF;
            m_nodes[iF].parent = iA;
        }
        a.bounds = Collision::UnionAabb(m_nodes[a.child0].bounds,
                                        m_nodes[a.child1].bounds);
        a.height = 1u + std::max(m_nodes[a.child0].height, m_nodes[a.child1].height);
        m_nodes[iC].bounds = Collision::UnionAabb(a.bounds,
                                                  m_nodes[m_nodes[iC].child1].bounds);
        m_nodes[iC].height =
            1u + std::max(a.height, m_nodes[m_nodes[iC].child1].height);
        return iC;
    }
    if (balance < -1) {
        const std::uint32_t iD = m_nodes[iB].child0;
        const std::uint32_t iE = m_nodes[iB].child1;
        m_nodes[iB].child0 = iA;
        m_nodes[iB].parent = a.parent;
        a.parent = iB;
        if (m_nodes[iB].parent != kNull) {
            if (m_nodes[m_nodes[iB].parent].child0 == iA) {
                m_nodes[m_nodes[iB].parent].child0 = iB;
            } else {
                m_nodes[m_nodes[iB].parent].child1 = iB;
            }
        } else {
            m_root = iB;
        }
        if (m_nodes[iD].height > m_nodes[iE].height) {
            m_nodes[iB].child1 = iD;
            a.child0 = iE;
            m_nodes[iE].parent = iA;
        } else {
            m_nodes[iB].child1 = iE;
            a.child0 = iD;
            m_nodes[iD].parent = iA;
        }
        a.bounds = Collision::UnionAabb(m_nodes[a.child0].bounds,
                                        m_nodes[a.child1].bounds);
        a.height = 1u + std::max(m_nodes[a.child0].height, m_nodes[a.child1].height);
        m_nodes[iB].bounds = Collision::UnionAabb(a.bounds,
                                                  m_nodes[m_nodes[iB].child1].bounds);
        m_nodes[iB].height =
            1u + std::max(a.height, m_nodes[m_nodes[iB].child1].height);
        return iB;
    }
    return iA;
}

} // namespace Concord::Spatial
