#ifndef CONCORD_AUDIOVOICEPOOL_H
#define CONCORD_AUDIOVOICEPOOL_H

#include "audio/runtime/AudioPlayback.h"
#include "audio/runtime/AudioVoice.h"
#include "audio/runtime/detail/AudioClipRegistry.h"

#include <cstdint>
#include <vector>

namespace Concord::Audio::Detail {

struct ActiveVoiceView {
    AudioVoiceHandle handle{};
    AudioClipHandle clip{};
    AudioPlayParams params{};
    std::uint64_t cursorFrame = 0;
};

class AudioVoicePool {
public:
    void Reset(std::uint32_t capacity);
    AudioVoiceHandle Play(AudioClipHandle clip, const AudioPlayParams& params,
                          bool loop, const AudioClipRegistry& clips,
                          std::uint64_t& rejectedCommands);
    AudioVoiceHandle StealOrPlay(AudioClipHandle clip, const AudioPlayParams& params,
                                 bool loop, const AudioClipRegistry& clips,
                                 std::uint64_t& rejectedCommands);
    bool Stop(AudioVoiceHandle handle);
    bool Pause(AudioVoiceHandle handle, bool paused);
    void StopAll(AudioBusId bus);
    bool SetGain(AudioVoiceHandle handle, float gain);
    bool SetPitch(AudioVoiceHandle handle, float pitch);
    bool SetBus(AudioVoiceHandle handle, AudioBusId bus);
    bool SetSpatialBlend(AudioVoiceHandle handle, float spatialBlend);
    bool SetSpatialState(AudioVoiceHandle handle, const AudioSourceState& source);
    bool QuerySource(AudioVoiceHandle handle, AudioSourceState& out) const noexcept;
    void StopVoicesUsingClip(AudioClipHandle clip);
    void CollectActive(std::vector<ActiveVoiceView>& out) const;
    void Advance(AudioVoiceHandle handle, std::uint32_t frames, std::uint32_t clipFrames);
    bool IsAlive(AudioVoiceHandle handle) const noexcept;
    std::uint32_t ActiveVoiceCount() const noexcept;
    std::uint32_t ActiveSpatialVoiceCount() const noexcept;

private:
    struct Slot {
        AudioClipHandle clip{};
        AudioPlayParams params{};
        std::uint64_t cursorFrame = 0;
        std::uint32_t generation = 1;
        bool active = false;
        bool paused = false;
    };

    Slot* Resolve(AudioVoiceHandle handle) noexcept;
    Slot* FindStealCandidate(std::uint8_t incomingPriority) noexcept;

    std::vector<Slot> m_slots;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOVOICEPOOL_H
