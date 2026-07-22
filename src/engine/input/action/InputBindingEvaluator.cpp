#include "engine/input/action/InputBindingEvaluator.h"

#include "engine/input/InputState.h"

#include <algorithm>
#include <cmath>

namespace Concord::InputDetail {

bool BindingSources::Any() const noexcept
{
    return mouseDeltaX || mouseDeltaY
        || std::any_of(keys.begin(), keys.end(), [](bool value) { return value; })
        || std::any_of(buttons.begin(), buttons.end(), [](bool value) { return value; });
}

void BindingSources::Merge(const BindingSources& other) noexcept
{
    for (std::size_t i = 0; i < keys.size(); ++i) {
        keys[i] = keys[i] || other.keys[i];
    }
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        buttons[i] = buttons[i] || other.buttons[i];
    }
    mouseDeltaX = mouseDeltaX || other.mouseDeltaX;
    mouseDeltaY = mouseDeltaY || other.mouseDeltaY;
}

ActionBindingSample SampleActionBinding(const ActionBinding& binding,
                                        const InputState& input,
                                        const BindingSources& unavailable,
                                        BindingSources& used)
{
    ActionBindingSample sample;
    if (binding.key != Key::Unknown) {
        const std::size_t index = static_cast<std::size_t>(binding.key);
        if (index < unavailable.keys.size() && !unavailable.keys[index]) {
            const bool down = input.IsKeyDown(binding.key);
            const bool pressed = input.WasKeyPressed(binding.key);
            const bool released = input.WasKeyReleased(binding.key);
            used.keys[index] = used.keys[index] || down || pressed || released;
            sample.down = sample.down || down;
            sample.pressed = sample.pressed || pressed;
            sample.released = sample.released || released;
        }
    }
    if (binding.button != MouseButton::Count) {
        const std::size_t index = static_cast<std::size_t>(binding.button);
        if (index < unavailable.buttons.size() && !unavailable.buttons[index]) {
            const bool down = input.IsMouseButtonDown(binding.button);
            const bool pressed = input.WasMouseButtonPressed(binding.button);
            const bool released = input.WasMouseButtonReleased(binding.button);
            used.buttons[index] = used.buttons[index] || down || pressed || released;
            sample.down = sample.down || down;
            sample.pressed = sample.pressed || pressed;
            sample.released = sample.released || released;
        }
    }
    return sample;
}

float SampleAxisBinding(const AxisBinding& binding, const InputState& input,
                        const BindingSources& unavailable, BindingSources& used)
{
    float value = 0.0f;
    if (binding.positiveKey != Key::Unknown) {
        const std::size_t index = static_cast<std::size_t>(binding.positiveKey);
        if (index < unavailable.keys.size() && !unavailable.keys[index]
            && input.IsKeyDown(binding.positiveKey)) {
            used.keys[index] = true;
            value += 1.0f;
        }
    }
    if (binding.negativeKey != Key::Unknown) {
        const std::size_t index = static_cast<std::size_t>(binding.negativeKey);
        if (index < unavailable.keys.size() && !unavailable.keys[index]
            && input.IsKeyDown(binding.negativeKey)) {
            used.keys[index] = true;
            value -= 1.0f;
        }
    }
    if (binding.positiveButton != MouseButton::Count) {
        const std::size_t index = static_cast<std::size_t>(binding.positiveButton);
        if (index < unavailable.buttons.size() && !unavailable.buttons[index]
            && input.IsMouseButtonDown(binding.positiveButton)) {
            used.buttons[index] = true;
            value += 1.0f;
        }
    }
    if (binding.negativeButton != MouseButton::Count) {
        const std::size_t index = static_cast<std::size_t>(binding.negativeButton);
        if (index < unavailable.buttons.size() && !unavailable.buttons[index]
            && input.IsMouseButtonDown(binding.negativeButton)) {
            used.buttons[index] = true;
            value -= 1.0f;
        }
    }
    if (binding.useMouseDeltaX || binding.useMouseDeltaY) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        input.MouseDelta(deltaX, deltaY);
        if (binding.useMouseDeltaX && !unavailable.mouseDeltaX) {
            used.mouseDeltaX = deltaX != 0.0f;
            value += deltaX;
        }
        if (binding.useMouseDeltaY && !unavailable.mouseDeltaY) {
            used.mouseDeltaY = deltaY != 0.0f;
            value += deltaY;
        }
    }

    const float magnitude = std::fabs(value);
    if (magnitude < binding.deadzone) {
        return 0.0f;
    }
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    const float adjusted = (magnitude - binding.deadzone)
        / std::max(1.0f - binding.deadzone, 1e-6f);
    return std::clamp(sign * std::min(adjusted, 1.0f) * binding.sensitivity,
                      -1.0f, 1.0f);
}

} // namespace Concord::InputDetail
