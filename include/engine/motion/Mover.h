#ifndef CONCORD_MOVER_H
#define CONCORD_MOVER_H

#include "Concord/CExport.h"
#include "engine/motion/Easing.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <functional>
#include <vector>

namespace Concord {

namespace Object {
class Node;
}

namespace Motion {

/**
 * Drives one Object::Node's local transform over time.
 *
 * A Mover combines two kinds of motion, applied every Update(deltaTime):
 *  - Steady velocities: a linear velocity (local units/second) and an angular
 *    velocity (Euler degrees/second) that run until changed or stopped.
 *  - One-shot tweens: MoveTo / RotateTo / ScaleTo animate the node from its
 *    value at the call moment to a target over a duration with an easing curve.
 *    Position, rotation and scale have independent tween slots, so a node can
 *    slide and spin at once. FollowPath walks a waypoint list at constant speed.
 *
 * The Mover borrows the node by reference and must not outlive it. It writes
 * only the node's local transform, so it composes with the scene graph the same
 * way manual SetPosition/Rotate calls do. Designed as the per-state actuator an
 * animation state machine will drive; usable directly today via Node::OnUpdate.
 */
class CENGINE_API Mover {
public:
    explicit Mover(Object::Node& node) noexcept;

    /** Local-space translation applied continuously, in units per second. */
    Mover& SetLinearVelocity(Vector3 unitsPerSecond) noexcept;

    /** Continuous rotation, in Euler degrees per second (pitch, yaw, roll). */
    Mover& SetAngularVelocity(Vector3 eulerDegreesPerSecond) noexcept;

    /** Tweens local position to @p target over @p seconds. */
    Mover& MoveTo(Vector3 target, float seconds, Easing curve = Easing::InOutQuad);

    /**
     * Tweens the local position by @p delta over @p seconds — target is the
     * node's current position plus `delta`, so callers can fire-and-forget
     * "kick the object 2 units up over 0.3s" without first reading its pose.
     */
    Mover& MoveBy(Vector3 delta, float seconds, Easing curve = Easing::InOutQuad);

    /** Tweens local rotation to @p target over @p seconds (shortest-arc slerp). */
    Mover& RotateTo(Quaternion target, float seconds, Easing curve = Easing::InOutQuad);

    /** Tweens local scale to @p target over @p seconds. */
    Mover& ScaleTo(Vector3 target, float seconds, Easing curve = Easing::InOutQuad);

    /**
     * Moves through @p waypoints at a constant @p unitsPerSecond, starting from
     * the node's current position. With @p loop the path repeats from the first
     * waypoint. Replaces any active position tween.
     */
    Mover& FollowPath(const std::vector<Vector3>& waypoints, float unitsPerSecond, bool loop = false);

    /** Callback fired each time a tween or a (non-looping) path completes. */
    void OnArrive(std::function<void()> callback);

    /** Clears every tween, the path and both velocities. */
    void Stop() noexcept;

    /** True while any tween or a path is still running. */
    bool IsBusy() const noexcept;

    /** Advances all active motion by @p deltaTime seconds and writes the node. */
    void Update(float deltaTime);

private:
    struct VectorTween {
        bool active = false;
        Vector3 from{};
        Vector3 to{};
        float elapsed = 0.0f;
        float duration = 0.0f;
        Easing curve = Easing::Linear;
    };
    struct QuatTween {
        bool active = false;
        Quaternion from{};
        Quaternion to{};
        float elapsed = 0.0f;
        float duration = 0.0f;
        Easing curve = Easing::Linear;
    };

    /** Advances @p tween, returns the current value, and flags completion. */
    Vector3 StepVector(VectorTween& tween, float deltaTime, bool& completed);

    Object::Node* m_node = nullptr;

    Vector3 m_linearVelocity{};
    Vector3 m_angularVelocity{};

    VectorTween m_positionTween;
    QuatTween m_rotationTween;
    VectorTween m_scaleTween;

    std::vector<Vector3> m_path;
    std::size_t m_pathTarget = 0;
    float m_pathSpeed = 0.0f;
    bool m_pathActive = false;
    bool m_pathLoop = false;

    std::function<void()> m_onArrive;
};

} // namespace Motion
} // namespace Concord

#endif // CONCORD_MOVER_H
