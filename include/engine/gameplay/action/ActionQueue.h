#ifndef CONCORD_ACTIONQUEUE_H
#define CONCORD_ACTIONQUEUE_H

#include "Concord/CExport.h"
#include "engine/gameplay/action/Action.h"

#include <functional>
#include <string>
#include <vector>

namespace Concord::Gameplay {

/**
 * @brief Runs one character action at a time: start rules, the action clock,
 * cooldowns, and animation-event bridging.
 *
 * Start rules: a request is rejected while its name is cooling down or when
 * the running action cannot be interrupted. A higher-priority request
 * interrupts a running action whose `interrupt` allows it; an equal-priority
 * request restarts an action whose `interrupt` is Self (the same name).
 * Ending (completion, interruption or Cancel) starts the cooldown.
 *
 * The action clock and lifecycle callbacks run inside Update; call
 * NotifyAnimationEvent from the animator's event callback so `triggerEvent`
 * markers on the action's clip reach the game logic once per frame they fire.
 */
class CENGINE_API ActionQueue {
public:
    /**
     * @brief Attempts to start @p desc.
     * @return false when the name is empty, the action is cooling down, or
     *         the running action cannot be interrupted. A successful start
     *         fires onBegin (after ending whatever was running).
     */
    bool TryStart(const ActionDesc& desc);

    /** Advances the running action and decays every cooldown. */
    void Update(float deltaTime);

    /** Ends the running action early (completion semantics, cooldown starts). */
    void Cancel();

    /** Name of the running action, or empty when none. */
    const std::string& CurrentAction() const noexcept;

    /** True when @p name is currently cooling down. */
    bool IsOnCooldown(const std::string& name) const noexcept;

    /** Remaining cooldown seconds for @p name, or 0. */
    float RemainingCooldown(const std::string& name) const noexcept;

    /**
     * @brief Feeds one animation event; fires the event callback when it
     * matches the running action's `triggerEvent`.
     */
    void NotifyAnimationEvent(const std::string& eventName);

    /** Registers the callback fired by a matching trigger event. */
    void SetEventCallback(std::function<void(const std::string& actionName)> callback)
    {
        m_eventCallback = std::move(callback);
    }

    /**
     * @brief Registers the callback fired when an action ends (completion,
     * interruption, or Cancel). Receives the action's description so the
     * caller can, e.g., return the animator to the action's `returnState`.
     */
    void SetEndCallback(std::function<void(const ActionDesc&)> callback)
    {
        m_endCallback = std::move(callback);
    }

private:
    /** Ends the running action, firing onEnd and starting its cooldown. */
    void EndCurrent();

    ActionInstance m_current;
    struct CooldownEntry {
        std::string name;
        float remaining = 0.0f;
    };
    std::vector<CooldownEntry> m_cooldowns;
    std::function<void(const std::string&)> m_eventCallback;
    std::function<void(const ActionDesc&)> m_endCallback;
};

} // namespace Concord::Gameplay

#endif // CONCORD_ACTIONQUEUE_H
