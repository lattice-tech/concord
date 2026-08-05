#include "engine/gameplay/action/ActionQueue.h"

#include <algorithm>
#include <utility>

namespace Concord::Gameplay {

bool ActionQueue::TryStart(const ActionDesc& desc)
{
    if (desc.name.empty() || IsOnCooldown(desc.name)) {
        return false;
    }
    if (m_current.active) {
        const ActionDesc& current = m_current.desc;
        const bool interrupted = desc.priority > current.priority
            && current.interrupt != ActionInterrupt::None;
        const bool restarted = desc.priority == current.priority
            && current.interrupt == ActionInterrupt::Self
            && desc.name == current.name;
        if (!interrupted && !restarted) {
            return false;
        }
        EndCurrent();
    }
    m_current = ActionInstance{desc, 0.0f, true};
    if (m_current.desc.onBegin) {
        m_current.desc.onBegin();
    }
    return true;
}

void ActionQueue::Update(float deltaTime)
{
    for (CooldownEntry& entry : m_cooldowns) {
        entry.remaining = std::max(0.0f, entry.remaining - deltaTime);
    }
    m_cooldowns.erase(
        std::remove_if(m_cooldowns.begin(), m_cooldowns.end(),
                       [](const CooldownEntry& entry) {
                           return entry.remaining <= 0.0f;
                       }),
        m_cooldowns.end());

    if (!m_current.active) {
        return;
    }
    m_current.elapsed += deltaTime;
    if (m_current.desc.onTick) {
        m_current.desc.onTick(deltaTime);
    }
    if (m_current.desc.duration > 0.0f
        && m_current.elapsed >= m_current.desc.duration) {
        EndCurrent();
    }
}

void ActionQueue::Cancel()
{
    EndCurrent();
}

const std::string& ActionQueue::CurrentAction() const noexcept
{
    static const std::string kNone;
    return m_current.active ? m_current.desc.name : kNone;
}

bool ActionQueue::IsOnCooldown(const std::string& name) const noexcept
{
    for (const CooldownEntry& entry : m_cooldowns) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

float ActionQueue::RemainingCooldown(const std::string& name) const noexcept
{
    for (const CooldownEntry& entry : m_cooldowns) {
        if (entry.name == name) {
            return entry.remaining;
        }
    }
    return 0.0f;
}

void ActionQueue::NotifyAnimationEvent(const std::string& eventName)
{
    if (!m_current.active || m_current.desc.triggerEvent.empty()
        || m_current.desc.triggerEvent != eventName) {
        return;
    }
    if (m_eventCallback) {
        m_eventCallback(m_current.desc.name);
    }
}

void ActionQueue::EndCurrent()
{
    if (!m_current.active) {
        return;
    }
    ActionDesc ended = std::move(m_current.desc);
    m_current = {};
    if (ended.onEnd) {
        ended.onEnd();
    }
    if (ended.cooldown > 0.0f) {
        for (CooldownEntry& entry : m_cooldowns) {
            if (entry.name == ended.name) {
                entry.remaining = std::max(entry.remaining, ended.cooldown);
                if (m_endCallback) {
                    m_endCallback(ended);
                }
                return;
            }
        }
        m_cooldowns.push_back(CooldownEntry{ended.name, ended.cooldown});
    }
    if (m_endCallback) {
        m_endCallback(ended);
    }
}

} // namespace Concord::Gameplay
