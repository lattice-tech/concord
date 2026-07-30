#ifndef CONCORD_AUDIODEVICECONFIG_H
#define CONCORD_AUDIODEVICECONFIG_H

#include <cstdint>

namespace Concord::Audio {

/** Runtime playback device configuration. */
struct AudioDeviceConfig {
    std::int32_t sampleRate = 48000;
    std::int32_t frameSize = 1024;
    std::int32_t bufferedFrames = 4096;
    bool startMuted = false;
    bool validation = false;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIODEVICECONFIG_H
