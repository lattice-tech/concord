#ifndef CONCORD_ACTION_H
#define CONCORD_ACTION_H

#include <cstdint>
#include <functional>
#include <string>

namespace Concord::Gameplay {

/** Who may interrupt a running action. */
enum class ActionInterrupt : std::uint8_t {
    None, ///< Runs to completion (or Cancel); never interrupted.
    Self, ///< A new request of the same name may restart it.
    Any,  ///< Any higher-priority action may interrupt it.
};

/**
 * @brief Authoring description of one character action.
 *
 * A plain aggregate: name it, give it a duration (0 = runs until ended) and
 * optional cooldown/priority/interrupt rules, then optionally bind an
 * animation state and a trigger event. Lifecycle callbacks fire on the
 * queue's thread: onBegin once when the action starts, onTick per frame,
 * onEnd when it completes, is interrupted, or is cancelled.
 */
struct ActionDesc {
    /** Unique action name; must be non-empty to start. */
    std::string name;

    /** Duration in seconds; 0 runs until cancelled or interrupted. */
    float duration = 0.0f;

    /** Seconds after the action ends during which it cannot restart. */
    float cooldown = 0.0f;

    /** Higher priority interrupts lower (subject to @p interrupt). */
    int priority = 0;

    /** Who may interrupt this action once it is running. */
    ActionInterrupt interrupt = ActionInterrupt::Self;

    /** Animator base-machine state to jump to on start (may be empty). */
    std::string animationState;

    /** Animator base-machine state to return to on end (may be empty). */
    std::string returnState;

    /**
     * Animation event name that fires the event callback while this action
     * runs (e.g. "hit" on an attack's impact frame).
     */
    std::string triggerEvent;

    std::function<void()> onBegin;
    std::function<void(float)> onTick;
    std::function<void()> onEnd;
};

/** One running action: the description plus its clock. */
struct ActionInstance {
    ActionDesc desc;
    float elapsed = 0.0f;
    bool active = false;
};

} // namespace Concord::Gameplay

#endif // CONCORD_ACTION_H
