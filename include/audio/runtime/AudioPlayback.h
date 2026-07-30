#ifndef CONCORD_AUDIOPLAYBACK_H
#define CONCORD_AUDIOPLAYBACK_H

#include "audio/runtime/AudioBus.h"
#include "audio/runtime/AudioSourceState.h"

#include <cstdint>

namespace Concord::Audio {

/** Initial playback parameters for a one-shot or looping voice. */
struct AudioPlayParams {
    AudioBusId bus = AudioBusId::Sfx;
    float gain = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    std::uint8_t priority = 128;
    bool spatial = false;
    float spatialBlend = 1.0f;
    AudioSourceState source{};
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOPLAYBACK_H
