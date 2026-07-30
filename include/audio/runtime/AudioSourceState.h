#ifndef CONCORD_AUDIOSOURCESTATE_H
#define CONCORD_AUDIOSOURCESTATE_H

#include "math/Vector3.h"

namespace Concord::Audio {

/** Distance attenuation model applied before HRTF spatialization. */
enum class AudioAttenuationModel : std::uint8_t {
    None = 0,
    Linear,
    Inverse,
};

/** Plain-data world-space source properties for one spatialized voice. */
struct AudioSourceState {
    Vector3 position{};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 velocity{};
    float gain = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    float nearGain = 1.0f;
    float farGain = 0.0f;
    float attenuationExponent = 1.0f;
    float spatialBlend = 1.0f;
    float dopplerScale = 1.0f;
    float innerConeDegrees = 360.0f;
    float outerConeDegrees = 360.0f;
    float outerConeGain = 1.0f;
    AudioAttenuationModel attenuation = AudioAttenuationModel::Linear;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOSOURCESTATE_H
