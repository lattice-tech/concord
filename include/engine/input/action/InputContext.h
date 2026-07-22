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
 * `priority` evaluates first; consuming an action blocks lower layers.
 */
struct InputContext {
    std::string name;
    InputContextPriority priority = InputContextPriority::Gameplay;
    std::vector<ActionBinding> actions;
    std::vector<AxisBinding> axes;
    /**
     * When true, any action that fires in this context is automatically
     * consumed so lower contexts do not also see it. Per-query Consume still
     * works when this is false.
     */
    bool consumeOnTrigger = true;
};

} // namespace Concord

#endif // CONCORD_INPUTCONTEXT_H
