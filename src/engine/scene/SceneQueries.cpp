#include "engine/scene/Scene.h"

#include "engine/collision/query/RayIntersection.h"
#include "engine/object/Collider.h"

#include <vector>

namespace Concord {

bool Scene::RaycastClosest(const Collision::Ray& ray,
                           const Collision::RaycastFilter& filter,
                           Collision::RaycastHit& outHit) const
{
    Collision::RaycastHit closest;
    if (!RaycastInternal(ray, filter, &closest)) {
        return false;
    }
    outHit = closest;
    return true;
}

bool Scene::RaycastAny(const Collision::Ray& ray,
                       const Collision::RaycastFilter& filter) const
{
    return RaycastInternal(ray, filter, nullptr);
}

bool Scene::RaycastInternal(const Collision::Ray& ray,
                            const Collision::RaycastFilter& filter,
                            Collision::RaycastHit* outClosest) const
{
    Collision::Ray normalizedRay;
    if (filter.layerMask == 0
        || !Collision::NormalizeRay(ray, normalizedRay)
        || !Collision::IsValidRaycastRange(filter.minDistance, filter.maxDistance)) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    std::vector<Object::Collider*> colliders;
    colliders.reserve(m_nodes.size());
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        if (ResolveLiveLocked(node->m_handle) != nullptr) {
            node->CollectColliders(colliders);
        }
    }

    bool found = false;
    Collision::RaycastHit closest;
    for (Object::Collider* collider : colliders) {
        const Object::ObjectId colliderId = collider->Id();
        if (colliderId == filter.ignoreColliderId
            || (collider->Layer() & filter.layerMask) == 0) {
            continue;
        }

        Collision::RaycastHit candidate;
        if (!Collision::IntersectRayShape(
                normalizedRay, collider->Shape(), collider->WorldMatrix(),
                filter.minDistance, filter.maxDistance, candidate)) {
            continue;
        }
        candidate.colliderId = colliderId;
        const Object::Node* parent = collider->Parent();
        candidate.objectId = parent != nullptr ? parent->Id() : colliderId;
        if (outClosest == nullptr) {
            return true;
        }
        if (!found || candidate.distance < closest.distance
            || (candidate.distance == closest.distance
                && candidate.colliderId < closest.colliderId)) {
            closest = candidate;
            found = true;
        }
    }
    if (found) {
        *outClosest = closest;
    }
    return found;
}

} // namespace Concord
