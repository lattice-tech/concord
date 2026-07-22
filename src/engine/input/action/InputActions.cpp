#include "engine/input/action/InputActions.h"

#include "engine/input/InputState.h"
#include "engine/input/action/InputBindingEvaluator.h"

#include <algorithm>
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

struct PendingActionSample {
    bool down = false;
    bool pressed = false;
    bool released = false;
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

    std::unordered_map<std::string, PendingActionSample> actionNow;
    std::unordered_map<std::string, float> axisNow;
    std::unordered_set<std::string> autoConsumedActions;
    std::unordered_set<std::string> autoConsumedAxes;
    InputDetail::BindingSources consumedInputs;

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

    for (std::size_t contextIndex = 0; contextIndex < ordered.size(); ++contextIndex) {
        const ContextEntry* entry = ordered[contextIndex];
        const InputContext& context = entry->context;
        InputDetail::BindingSources usedByContext;
        std::unordered_set<std::string> triggeredActions;
        std::unordered_set<std::string> triggeredAxes;
        for (const ActionBinding& binding : context.actions) {
            if (binding.action.name.empty()) {
                continue;
            }
            if (autoConsumedActions.count(binding.action.name) != 0) {
                continue;
            }
            const InputDetail::ActionBindingSample bindingSample =
                InputDetail::SampleActionBinding(
                    binding, input, consumedInputs, usedByContext);
            if (!bindingSample.Any()) {
                continue;
            }
            PendingActionSample& action = actionNow[binding.action.name];
            action.down = action.down || bindingSample.down;
            action.pressed = action.pressed || bindingSample.pressed;
            action.released = action.released || bindingSample.released;
            triggeredActions.insert(binding.action.name);
        }
        for (const AxisBinding& binding : context.axes) {
            if (binding.axis.name.empty()) {
                continue;
            }
            if (autoConsumedAxes.count(binding.axis.name) != 0) {
                continue;
            }
            InputDetail::BindingSources usedByAxis;
            const float contribution = InputDetail::SampleAxisBinding(
                binding, input, consumedInputs, usedByAxis);
            if (usedByAxis.Any()) {
                usedByContext.Merge(usedByAxis);
                triggeredAxes.insert(binding.axis.name);
            }
            axisNow[binding.axis.name] = axisNow[binding.axis.name] + contribution;
        }

        if (context.consumeOnTrigger) {
            consumedInputs.Merge(usedByContext);
            autoConsumedActions.insert(triggeredActions.begin(), triggeredActions.end());
            autoConsumedAxes.insert(triggeredAxes.begin(), triggeredAxes.end());
        }

        if (context.blocksLowerContexts) {
            break;
        }
    }

    for (auto& [name, value] : axisNow) {
        value = std::clamp(value, -1.0f, 1.0f);
    }

    std::unordered_map<std::string, ActionSample> nextActions;
    for (const auto& [name, pending] : actionNow) {
        ActionSample sample;
        sample.down = pending.down;
        const auto previous = manager.prevDown.find(name);
        const bool wasDown = previous != manager.prevDown.end() && previous->second;
        sample.pressed = pending.pressed && !wasDown;
        sample.released = pending.released && !sample.down
            && (wasDown || sample.pressed);
        sample.consumed = false;
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
    return it != manager.actions.end() && it->second.released && !it->second.consumed;
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
