#include "engine/app/UpdateSubscription.h"

#include <utility>

namespace Concord {

void UpdateSubscription::State::Reset()
{
    const EngineLoop::UpdateId updateId = id.exchange(EngineLoop::kInvalidUpdateId);
    if (updateId == EngineLoop::kInvalidUpdateId) {
        return;
    }
    if (const std::shared_ptr<EngineLoop> updateLoop = loop.lock()) {
        updateLoop->RemoveUpdate(updateId);
    }
}

UpdateSubscription::UpdateSubscription(std::shared_ptr<State> state) noexcept
    : m_state(std::move(state))
{
}

UpdateSubscription::~UpdateSubscription()
{
    Reset();
}

UpdateSubscription::UpdateSubscription(UpdateSubscription&& other) noexcept
    : m_state(std::move(other.m_state))
{
}

UpdateSubscription& UpdateSubscription::operator=(UpdateSubscription&& other) noexcept
{
    if (this != &other) {
        Reset();
        m_state = std::move(other.m_state);
    }
    return *this;
}

void UpdateSubscription::Reset()
{
    if (m_state) {
        m_state->Reset();
        m_state.reset();
    }
}

UpdateSubscription::operator bool() const noexcept
{
    return m_state
        && m_state->id.load() != EngineLoop::kInvalidUpdateId;
}

} // namespace Concord
