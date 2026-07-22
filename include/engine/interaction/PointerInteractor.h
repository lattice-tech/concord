#ifndef CONCORD_POINTERINTERACTOR_H
#define CONCORD_POINTERINTERACTOR_H

#include "Concord/CExport.h"
#include "engine/interaction/PointerInteractionFeedback.h"
#include "engine/interaction/PointerInteractionInput.h"

namespace Concord {

class Scene;

namespace Object {
class Camera;
}

namespace Interaction {

/**
 * @brief Resolves viewport pointer input into stable scene interaction state.
 *
 * Update is intended for the simulation coordinator and stores only copied
 * hit data. UI blocking, cancellation, and pointer departure clear hover and
 * press capture before any camera or scene query occurs.
 */
class CENGINE_API PointerInteractor {
public:
    /**
     * @brief Advances hover, press capture, release, and activation by one frame.
     *
     * Global hover-change and activation events are best-effort notifications;
     * queue rejection does not alter the returned local state.
     */
    PointerInteractionFeedback Update(Scene& scene, Object::Camera& camera,
                                      const PointerInteractionInput& input);

private:
    void SetHovered(const Collision::RaycastHit* hit);
    PointerInteractionFeedback Feedback(bool activated, bool blocked) const noexcept;

    Collision::RaycastHit m_hoveredHit{};
    Collision::RaycastHit m_pressedHit{};
    bool m_hasHovered = false;
    bool m_hasPressed = false;
};

} // namespace Interaction
} // namespace Concord

#endif // CONCORD_POINTERINTERACTOR_H
