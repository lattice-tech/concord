#ifndef CONCORD_AUDIOLISTENERSTATE_H
#define CONCORD_AUDIOLISTENERSTATE_H

#include "math/Vector3.h"

namespace Concord::Audio {

/** Plain-data listener pose and velocity consumed by the audio runtime. */
struct AudioListenerState {
    Vector3 position{};
    Vector3 right{1.0f, 0.0f, 0.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 velocity{};
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOLISTENERSTATE_H
