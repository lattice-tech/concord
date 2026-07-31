#ifndef CONCORD_AUDIOSTATSBOARD_H
#define CONCORD_AUDIOSTATSBOARD_H

#include "audio/runtime/AudioStats.h"

#include <atomic>
#include <cstdint>

namespace Concord::Audio::Detail {

/**
 * @brief Shared measurement board written by the mixing thread and read by
 * the update thread.
 *
 * Every field is an independent relaxed atomic: each one is a monotonically
 * advancing counter or a latest-value gauge, so readers only need a torn-free
 * value per field, never a consistent cross-field snapshot. Snapshot() copies
 * the board into the plain AudioStats struct handed out through the public
 * API.
 */
class AudioStatsBoard {
public:
    void Reset() noexcept;

    /** Copies every gauge/counter into the public plain struct. */
    AudioStats Snapshot() const noexcept;

    std::atomic<bool> initialized{false};
    std::atomic<std::uint32_t> activeVoices{0};
    std::atomic<std::uint32_t> activeSpatialVoices{0};
    std::atomic<std::uint32_t> queuedCommands{0};
    std::atomic<std::uint64_t> rejectedCommands{0};
    std::atomic<std::uint64_t> underrunCount{0};
    std::atomic<std::uint64_t> recoverCount{0};
    std::atomic<float> callbackCpuMs{0.0f};
    std::atomic<float> peakMaster{0.0f};
    std::atomic<float> peakMusic{0.0f};
    std::atomic<float> peakSfx{0.0f};
    std::atomic<float> peakUi{0.0f};
    std::atomic<float> peakDialogue{0.0f};
    std::atomic<float> peakAmbience{0.0f};
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOSTATSBOARD_H
