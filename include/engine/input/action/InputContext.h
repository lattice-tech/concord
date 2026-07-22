#ifndef CONCORD_INPUTCONTEXT_H
#define CONCORD_INPUTCONTEXT_H

#include "engine/input/action/InputBinding.h"
#include "engine/input/action/InputContextPriority.h"

#include <string>
#include <vector>

namespace Concord {

/**
 * @brief One layer of remappable action / axis bindings.
 *
 * Pushed onto the process-wide stack with `InputActions::PushContext`. Higher
 * `priority` evaluates first. Consumed physical inputs are hidden from lower
 * layers even when those layers use a different action or axis name.
 */
struct InputContext {
    std::string name;
    InputContextPriority priority = InputContextPriority::Gameplay;
    std::vector<ActionBinding> actions;
    std::vector<AxisBinding> axes;
    /**
     * When true, triggered action and axis bindings consume their physical
     * sources and logical names before lower contexts are evaluated. Per-query
     * `InputActions::Consume` remains available for action results.
     */
    bool consumeOnTrigger = true;

    /**
     * Prevents every lower-priority context from observing input while this
     * context is present. Use this for modal UI, consoles, and pause screens.
     */
    bool blocksLowerContexts = false;
};

} // namespace Concord

#endif // CONCORD_INPUTCONTEXT_H
