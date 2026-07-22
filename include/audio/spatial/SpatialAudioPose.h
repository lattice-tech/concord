#ifndef CONCORD_SPATIALAUDIOPOSE_H
#define CONCORD_SPATIALAUDIOPOSE_H

#include "math/Vector3.h"

namespace Concord::Audio {

/** @brief World-space listener basis used to transform sources into HRTF space. */
struct SpatialAudioListener {
    Vector3 position{};
    Vector3 right{1.0f, 0.0f, 0.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 forward{0.0f, 0.0f, 1.0f};
};

/** @brief Per-frame spatial properties of one mono source. */
struct SpatialAudioSource {
    Vector3 position{};
    float gain = 1.0f;
    float spatialBlend = 1.0f;
};

} // namespace Concord::Audio

#endif // CONCORD_SPATIALAUDIOPOSE_H
