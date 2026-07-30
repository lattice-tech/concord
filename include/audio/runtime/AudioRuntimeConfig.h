#ifndef CONCORD_AUDIORUNTIMECONFIG_H
#define CONCORD_AUDIORUNTIMECONFIG_H

#include "audio/runtime/AudioDeviceConfig.h"

#include <cstdint>

namespace Concord::Audio {

/** High-level runtime capacity and device configuration. */
struct AudioRuntimeConfig {
    AudioDeviceConfig device{};
    std::uint32_t maxClips = 1024;
    std::uint32_t maxVoices = 256;
    std::uint32_t maxSpatialVoices = 64;
    std::uint32_t commandQueueCapacity = 4096;
    std::uint32_t streamBufferFrames = 8192;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIORUNTIMECONFIG_H
