#ifndef CONCORD_SPATIALAUDIOCONFIG_H
#define CONCORD_SPATIALAUDIOCONFIG_H

#include <cstdint>

namespace Concord::Audio {

/** @brief Immutable processing format used by a Steam Audio spatializer. */
struct SpatialAudioConfig {
    std::int32_t sampleRate = 48000;
    std::int32_t frameSize = 1024;
    bool validation = false;
};

} // namespace Concord::Audio

#endif // CONCORD_SPATIALAUDIOCONFIG_H
