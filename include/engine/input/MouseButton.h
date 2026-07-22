#ifndef CONCORD_MOUSEBUTTON_H
#define CONCORD_MOUSEBUTTON_H

#include <cstdint>

namespace Concord {

/**
 * A mouse button. `X1`/`X2` are the extra side buttons found on many mice.
 * `Count` is a sentinel sizing the internal state arrays and is never a button.
 */
enum class MouseButton : std::uint8_t {
    Left = 0,
    Right,
    Middle,
    X1,
    X2,

    Count,
};

} // namespace Concord

#endif // CONCORD_MOUSEBUTTON_H
