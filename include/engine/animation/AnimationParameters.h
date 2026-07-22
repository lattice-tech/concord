#ifndef CONCORD_ANIMATIONPARAMETERS_H
#define CONCORD_ANIMATIONPARAMETERS_H

#include <string>
#include <unordered_map>

namespace Concord::Animation {

/**
 * The named values a state machine's transitions test and a game's logic
 * drives — the animation-graph equivalent of Unity's Animator parameters.
 *
 * Three kinds share one store:
 *   - float  : continuous inputs (speed, health) blend spaces read and
 *              threshold conditions compare against.
 *   - bool   : latched flags (isGrounded, isAiming).
 *   - trigger: one-shot events (jump, hit) that a transition consumes when it
 *              fires, then auto-reset — so a trigger set this frame causes at
 *              most one transition and never sticks.
 */
class AnimationParameters {
public:
    void SetFloat(const std::string& name, float value) { m_floats[name] = value; }
    void SetBool(const std::string& name, bool value) { m_bools[name] = value; }
    /** Latches a one-shot trigger; cleared when a transition consumes it. */
    void SetTrigger(const std::string& name) { m_triggers[name] = true; }

    float GetFloat(const std::string& name) const
    {
        const auto it = m_floats.find(name);
        return it != m_floats.end() ? it->second : 0.0f;
    }
    bool GetBool(const std::string& name) const
    {
        const auto it = m_bools.find(name);
        return it != m_bools.end() && it->second;
    }
    bool GetTrigger(const std::string& name) const
    {
        const auto it = m_triggers.find(name);
        return it != m_triggers.end() && it->second;
    }

    /** Clears a trigger after a transition has consumed it. */
    void ConsumeTrigger(const std::string& name) { m_triggers[name] = false; }

private:
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool> m_bools;
    std::unordered_map<std::string, bool> m_triggers;
};

} // namespace Concord::Animation

#endif // CONCORD_ANIMATIONPARAMETERS_H
