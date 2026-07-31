#ifndef CONCORD_AUDIOCOMMANDQUEUE_H
#define CONCORD_AUDIOCOMMANDQUEUE_H

#include "audio/runtime/AudioBus.h"
#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/AudioSourceState.h"
#include "audio/runtime/detail/AudioStatsBoard.h"
#include "audio/runtime/AudioVoice.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace Concord::Audio::Detail {

class AudioMixerState;
class AudioVoicePool;

/** Discriminates the payload carried by one AudioCommand. */
enum class AudioCommandKind : std::uint8_t {
    SetListener = 0,
    SetVoiceGain,
    SetVoicePitch,
    SetVoiceBus,
    SetVoiceSpatialBlend,
    SetVoiceSpatialState,
    PatchVoicePosition,
    PatchVoiceForward,
    PatchVoiceVelocity,
    PatchVoiceOcclusion,
    SetBusGain,
    SetBusMute,
};

/**
 * @brief Plain-data control command routed from the update thread to the
 * mixing thread.
 *
 * Every payload is trivially copyable so pushing a command never allocates
 * and applying one never runs user code; validation of values happens on the
 * producer side, handle liveness is re-checked at apply time.
 */
struct AudioCommand {
    AudioCommandKind kind = AudioCommandKind::SetListener;
    AudioBusId bus = AudioBusId::Master;
    bool flag = false;
    float value = 0.0f;
    AudioVoiceHandle voice{};
    Vector3 vec{};
    AudioListenerState listener{};
    AudioSourceState source{};
};

/**
 * @brief Fixed-capacity single-producer single-consumer command ring.
 *
 * The update thread pushes, the mixing thread drains at the start of each
 * render block; neither side ever blocks or allocates after Init. When the
 * ring is momentarily full the runtime falls back to applying the command
 * directly under the render lock, so no command is ever lost.
 */
class AudioCommandQueue {
public:
    void Init(std::uint32_t capacity);
    void Reset() noexcept;

    /** Producer side; returns false when the ring is full. */
    bool TryPush(const AudioCommand& command) noexcept;

    /** Consumer side; returns false when the ring is empty. */
    bool TryPop(AudioCommand& out) noexcept;

    std::uint32_t ApproxDepth() const noexcept;

private:
    std::vector<AudioCommand> m_slots;
    std::uint32_t m_mask = 0;
    std::atomic<std::uint64_t> m_head{0};
    std::atomic<std::uint64_t> m_tail{0};
};

/** Applies one command to the runtime state; called on the mixing thread. */
void ApplyAudioCommand(const AudioCommand& command, AudioListenerState& listener,
                       AudioVoicePool& voices, AudioMixerState& mixerState) noexcept;

/** Drains the queue on the mixing thread before a render block. */
void DrainAudioCommands(AudioCommandQueue& queue, AudioListenerState& listener,
                        AudioVoicePool& voices, AudioMixerState& mixerState,
                        AudioStatsBoard& stats) noexcept;

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOCOMMANDQUEUE_H
