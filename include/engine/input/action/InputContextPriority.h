#ifndef CONCORD_INPUTCONTEXTPRIORITY_H
#define CONCORD_INPUTCONTEXTPRIORITY_H

#include <cstdint>

namespace Concord {

/**
 * @brief Built-in priority bands for the input context stack.
 *
 * Higher numeric values win. When an action is consumed in a higher band,
 * lower contexts do not observe that action for the rest of the frame.
 */
enum class InputContextPriority : std::int32_t {
    Gameplay = 0,
    Ui = 100,
    Console = 200,
    Editor = 300,
};

} // namespace Concord

#endif // CONCORD_INPUTCONTEXTPRIORITY_H
