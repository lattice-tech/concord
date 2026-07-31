#ifndef CONCORD_AUDIOMIXSNAPSHOT_H
#define CONCORD_AUDIOMIXSNAPSHOT_H

#include "audio/runtime/AudioBus.h"

#include <array>

namespace Concord::Audio {

/**
 * @brief Named preset of every bus gain, applied as one atomic mix change.
 *
 * Snapshots capture moods ("underwater", "menu", "combat") as plain data.
 * Applying one interpolates every bus from its current gain to the snapshot
 * gain over the requested fade time, so the whole mix moves together.
 */
struct AudioMixSnapshotDesc {
    std::array<float, kAudioBusCount> busGains{1.0f, 1.0f, 1.0f,
                                               1.0f, 1.0f, 1.0f};

    float& Gain(AudioBusId bus) noexcept
    {
        return busGains[static_cast<std::size_t>(bus)];
    }

    float Gain(AudioBusId bus) const noexcept
    {
        return busGains[static_cast<std::size_t>(bus)];
    }
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOMIXSNAPSHOT_H
