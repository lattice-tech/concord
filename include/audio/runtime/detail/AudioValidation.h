#ifndef CONCORD_AUDIOVALIDATION_H
#define CONCORD_AUDIOVALIDATION_H

#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/AudioSourceState.h"

#include <cmath>

namespace Concord::Audio::Detail {

inline bool IsFinite(const Vector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool IsFinite(const AudioListenerState& listener) noexcept
{
    return IsFinite(listener.position) && IsFinite(listener.right)
        && IsFinite(listener.up) && IsFinite(listener.forward)
        && IsFinite(listener.velocity);
}

inline bool IsFinite(const AudioSourceState& source) noexcept
{
    return IsFinite(source.position) && IsFinite(source.forward) && IsFinite(source.velocity)
        && std::isfinite(source.gain) && std::isfinite(source.minDistance)
        && std::isfinite(source.maxDistance) && std::isfinite(source.nearGain)
        && std::isfinite(source.farGain) && std::isfinite(source.attenuationExponent)
        && std::isfinite(source.spatialBlend) && std::isfinite(source.innerConeDegrees)
        && std::isfinite(source.outerConeDegrees) && std::isfinite(source.outerConeGain);
}

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOVALIDATION_H
