#include "engine/interaction/PointerInteractor.h"

#include "engine/events/Events.h"
#include "engine/interaction/PointerInteractionEvents.h"
#include "engine/object/Camera.h"
#include "engine/scene/Scene.h"

#include <cmath>
#include <utility>

namespace Concord::Interaction {

namespace {

bool HasValidViewportPoint(const PointerInteractionInput& input) noexcept
{
    return std::isfinite(input.pixelX) && std::isfinite(input.pixelY)
        && std::isfinite(input.viewportWidth) && std::isfinite(input.viewportHeight)
        && input.viewportWidth > 0.0f && input.viewportHeight > 0.0f
        && input.pixelX >= 0.0f && input.pixelY >= 0.0f
        && input.pixelX < input.viewportWidth && input.pixelY < input.viewportHeight;
}

bool SameTarget(const Collision::RaycastHit& left,
                const Collision::RaycastHit& right) noexcept
{
    return left.objectId == right.objectId && left.colliderId == right.colliderId;
}

PointerHoverChangedEvent MakeHoverEvent(const Collision::RaycastHit* hit) noexcept
{
    if (hit == nullptr) {
        return {};
    }
    return {
        .objectId = hit->objectId,
        .colliderId = hit->colliderId,
        .position = hit->position,
        .normal = hit->normal,
        .distance = hit->distance,
    };
}

PointerActivatedEvent MakeActivatedEvent(const Collision::RaycastHit& hit) noexcept
{
    return {
        .objectId = hit.objectId,
        .colliderId = hit.colliderId,
        .position = hit.position,
        .normal = hit.normal,
        .distance = hit.distance,
    };
}

template <typename Event>
void PublishBestEffort(Event event) noexcept
{
    try {
        (void)Events::Publish(std::move(event));
    } catch (...) {
        // Local interaction state is authoritative when notification allocation fails.
    }
}

} // namespace

PointerInteractionFeedback PointerInteractor::Update(
    Scene& scene, Object::Camera& camera, const PointerInteractionInput& input)
{
    if (input.blockedByUi || input.cancelled || !input.pointerValid
        || !HasValidViewportPoint(input)) {
        SetHovered(nullptr);
        m_hasPressed = false;
        m_pressedHit = {};
        return Feedback(false, input.blockedByUi);
    }

    Collision::Ray ray;
    if (!camera.ScreenPointToRay(input.pixelX, input.pixelY,
                                 input.viewportWidth, input.viewportHeight, ray)) {
        SetHovered(nullptr);
        m_hasPressed = false;
        m_pressedHit = {};
        return Feedback(false, false);
    }

    Collision::RaycastHit hit;
    if (scene.RaycastClosest(ray, input.filter, hit)) {
        SetHovered(&hit);
    } else {
        SetHovered(nullptr);
    }

    if (input.pressed) {
        m_hasPressed = m_hasHovered;
        m_pressedHit = m_hasHovered ? m_hoveredHit : Collision::RaycastHit{};
    }

    bool activated = false;
    if (input.released) {
        activated = m_hasPressed && m_hasHovered
            && SameTarget(m_pressedHit, m_hoveredHit);
        m_hasPressed = false;
        m_pressedHit = {};
        if (activated) {
            PublishBestEffort(MakeActivatedEvent(m_hoveredHit));
        }
    }

    return Feedback(activated, false);
}

void PointerInteractor::SetHovered(const Collision::RaycastHit* hit)
{
    const bool hasNewHover = hit != nullptr;
    const bool changed = hasNewHover != m_hasHovered
        || (hit != nullptr && m_hasHovered && !SameTarget(*hit, m_hoveredHit));
    if (hit != nullptr) {
        m_hoveredHit = *hit;
        m_hasHovered = true;
    } else {
        m_hoveredHit = {};
        m_hasHovered = false;
    }
    if (changed) {
        PublishBestEffort(MakeHoverEvent(hit));
    }
}

PointerInteractionFeedback PointerInteractor::Feedback(bool activated, bool blocked) const noexcept
{
    PointerInteractionFeedback feedback;
    feedback.hit = m_hasHovered ? m_hoveredHit : Collision::RaycastHit{};
    feedback.pressedObjectId = m_hasPressed
        ? m_pressedHit.objectId : Object::kInvalidObjectId;
    feedback.pressedColliderId = m_hasPressed
        ? m_pressedHit.colliderId : Object::kInvalidObjectId;
    feedback.hovered = m_hasHovered;
    feedback.pressed = m_hasPressed;
    feedback.activated = activated;
    feedback.blocked = blocked;
    return feedback;
}

} // namespace Concord::Interaction
