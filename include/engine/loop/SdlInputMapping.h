#ifndef CONCORD_SDLINPUTMAPPING_H
#define CONCORD_SDLINPUTMAPPING_H

#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"

#include <SDL3/SDL.h>

#include <cstdint>

namespace Concord {

/** Maps an SDL scancode onto Concord's layout-independent Key; unmapped keys yield Key::Unknown. */
Key KeyFromScancode(SDL_Scancode scancode);

/** Maps an SDL mouse button index onto Concord's MouseButton (Count when unknown). */
MouseButton MouseButtonFromSdl(std::uint8_t button);

} // namespace Concord

#endif // CONCORD_SDLINPUTMAPPING_H
