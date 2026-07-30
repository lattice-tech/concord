#ifndef CONCORD_AUDIOSTATS_H
#define CONCORD_AUDIOSTATS_H

#include <cstdint>

namespace Concord::Audio {

/** Thread-safe runtime measurements copied out of CAudio.dll. */
struct AudioStats {
    bool initialized = false;
    std::uint32_t activeVoices = 0;
    std::uint32_t activeSpatialVoices = 0;
    std::uint32_t queuedCommands = 0;
    std::uint64_t rejectedCommands = 0;
    std::uint64_t underrunCount = 0;
    std::uint64_t recoverCount = 0;
    float callbackCpuMs = 0.0f;
    float peakMaster = 0.0f;
    float peakMusic = 0.0f;
    float peakSfx = 0.0f;
    float peakUi = 0.0f;
    float peakDialogue = 0.0f;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOSTATS_H
