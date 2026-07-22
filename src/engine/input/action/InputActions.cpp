#include "engine/input/action/InputActions.h"

#include "engine/input/InputState.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Concord {

namespace {

struct ContextEntry {
    std::uint64_t handle = 0;
    InputContext context;
};

struct ActionSample {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool consumed = false;
};

struct Manager {
    std::mutex mutex;
    std::vector<ContextEntry> stack;
    std::uint64_t nextHandle = 1;
    std::unordered_map<std::string, ActionSample> actions;
    std::unordered_map<std::string, float> axes;
    std::unordered_map<std::string, bool> prevDown;
};

Manager& Get()
{
    static Manager manager;
    return manager;
}

bool BindingDown(const ActionBinding& binding, const InputState& input)
{
    if (binding.key != Key::Unknown && input.IsKeyDown(binding.key)) {
        return true;
    }
    if (binding.button != MouseButton::Count && input.IsMouseButtonDown(binding.button)) {
        return true;
    }
    return false;
}

float AxisContribution(const AxisBinding& binding, const InputState& input)
{
    float value = 0.0f;
    if (binding.positiveKey != Key::Unknown && input.IsKeyDown(binding.positiveKey)) {
        value += 1.0f;
    }
    if (binding.negativeKey != Key::Unknown && input.IsKeyDown(binding.negativeKey)) {
        value -= 1.0f;
    }
    if (binding.positiveButton != MouseButton::Count
        && input.IsMouseButtonDown(binding.positiveButton)) {
        value += 1.0f;
    }
    if (binding.negativeButton != MouseButton::Count
        && input.IsMouseButtonDown(binding.negativeButton)) {
        value -= 1.0f;
    }
    if (binding.useMouseDeltaX || binding.useMouseDeltaY) {
        float dx = 0.0f;
        float dy = 0.0f;
        input.MouseDelta(dx, dy);
        if (binding.useMouseDeltaX) {
            value += dx;
        }
        if (binding.useMouseDeltaY) {
            value += dy;
        }
    }

    const float magnitude = std::fabs(value);
    if (magnitude < binding.deadzone) {
        return 0.0f;
    }
    // Remap so values just above deadzone start near zero.
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    const float adjusted = (magnitude - binding.deadzone) / std::max(1.0f - binding.deadzone, 1e-6f);
    value = sign * std::min(adjusted, 1.0f) * binding.sensitivity;
    return std::clamp(value, -1.0f, 1.0f);
}

} // namespace

std::uint64_t InputActions::PushContext(InputContext context)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const std::uint64_t handle = manager.nextHandle++;
    manager.stack.push_back(ContextEntry{handle, std::move(context)});
    return handle;
}

void InputActions::PopContext(std::uint64_t handle)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    std::erase_if(manager.stack, [handle](const ContextEntry& entry) {
        return entry.handle == handle;
    });
}

void InputActions::Clear()
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.stack.clear();
    manager.actions.clear();
    manager.axes.clear();
    manager.prevDown.clear();
}

void InputActions::UpdateFromInputState()
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const InputState& input = InputState::Instance();

    std::unordered_map<std::string, bool> downNow;
    std::unordered_map<std::string, float> axisNow;
    std::unordered_set<std::string> autoConsumed;

    // Higher priority first; later push wins ties.
    std::vector<const ContextEntry*> ordered;
    ordered.reserve(manager.stack.size());
    for (const ContextEntry& entry : manager.stack) {
        ordered.push_back(&entry);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ContextEntry* a, const ContextEntry* b) {
                         if (a->context.priority != b->context.priority) {
                             return static_cast<std::int32_t>(a->context.priority)
                                 > static_cast<std::int32_t>(b->context.priority);
                         }
                         return a->handle > b->handle;
                     });

    for (const ContextEntry* entry : ordered) {
        const InputContext& context = entry->context;
        for (const ActionBinding& binding : context.actions) {
            if (binding.action.name.empty()) {
                continue;
            }
            if (autoConsumed.count(binding.action.name) != 0) {
                continue;
            }
            if (!BindingDown(binding, input)) {
                continue;
            }
            downNow[binding.action.name] = true;
            if (context.consumeOnTrigger) {
                autoConsumed.insert(binding.action.name);
            }
        }
        for (const AxisBinding& binding : context.axes) {
            if (binding.axis.name.empty()) {
                continue;
            }
            const float contribution = AxisContribution(binding, input);
            axisNow[binding.axis.name] = axisNow[binding.axis.name] + contribution;
        }
    }

    for (auto& [name, value] : axisNow) {
        value = std::clamp(value, -1.0f, 1.0f);
    }

    std::unordered_map<std::string, ActionSample> nextActions;
    for (const auto& [name, down] : downNow) {
        ActionSample sample;
        sample.down = down;
        const bool wasDown = manager.prevDown[name];
        sample.pressed = down && !wasDown;
        sample.released = !down && wasDown;
        sample.consumed = false;
        nextActions.emplace(name, sample);
    }
    // Releases for actions that dropped out entirely.
    for (const auto& [name, wasDown] : manager.prevDown) {
        if (!wasDown || nextActions.count(name) != 0) {
            continue;
        }
        ActionSample sample;
        sample.down = false;
        sample.pressed = false;
        sample.released = true;
        nextActions.emplace(name, sample);
    }

    manager.actions = std::move(nextActions);
    manager.axes = std::move(axisNow);
    manager.prevDown.clear();
    for (const auto& [name, sample] : manager.actions) {
        manager.prevDown[name] = sample.down;
    }
}

bool InputActions::IsActionDown(const ActionId& action)
{
    return IsActionDown(std::string_view{action.name});
}

bool InputActions::IsActionDown(std::string_view action)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const auto it = manager.actions.find(std::string(action));
    return it != manager.actions.end() && it->second.down && !it->second.consumed;
}

bool InputActions::WasActionPressed(const ActionId& action)
{
    return WasActionPressed(std::string_view{action.name});
}

bool InputActions::WasActionPressed(std::string_view action)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const auto it = manager.actions.find(std::string(action));
    return it != manager.actions.end() && it->second.pressed && !it->second.consumed;
}

bool InputActions::WasActionReleased(const ActionId& action)
{
    return WasActionReleased(std::string_view{action.name});
}

bool InputActions::WasActionReleased(std::string_view action)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const auto it = manager.actions.find(std::string(action));
    return it != manager.actions.end() && it->second.released;
}

void InputActions::Consume(const ActionId& action)
{
    Consume(std::string_view{action.name});
}

void InputActions::Consume(std::string_view action)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const auto it = manager.actions.find(std::string(action));
    if (it != manager.actions.end()) {
        it->second.consumed = true;
    }
}

float InputActions::GetAxis(const AxisId& axis)
{
    return GetAxis(std::string_view{axis.name});
}

float InputActions::GetAxis(std::string_view axis)
{
    Manager& manager = Get();
    std::lock_guard<std::mutex> lock(manager.mutex);
    const auto it = manager.axes.find(std::string(axis));
    return it != manager.axes.end() ? it->second : 0.0f;
}

} // namespace Concord
