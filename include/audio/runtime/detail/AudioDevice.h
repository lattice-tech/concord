#ifndef CONCORD_AUDIODEVICE_H
#define CONCORD_AUDIODEVICE_H

#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/detail/AudioStatsBoard.h"
#include "audio/runtime/detail/AudioClipRegistry.h"
#include "audio/runtime/detail/AudioCommandQueue.h"
#include "audio/runtime/detail/AudioMixer.h"
#include "audio/runtime/detail/AudioMixerState.h"
#include "audio/runtime/detail/AudioVoicePool.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct SDL_AudioStream;

namespace Concord::Audio::Detail {

class AudioDevice {
public:
    /**
     * `renderLock` guards every object the mixing thread touches (voices,
     * clips, listener, buses); the runtime locks it on the main thread too,
     * so mixing never reads torn state.
     */
    bool Init(const AudioRuntimeConfig& config, AudioMixer& mixer,
              AudioListenerState* listener, const AudioClipRegistry* clips,
              AudioVoicePool* voices, AudioMixerState* buses,
              AudioCommandQueue* commands, AudioStatsBoard* stats,
              std::recursive_mutex* renderLock);
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept { return m_stream != nullptr; }
    void Pump() noexcept;

private:
    void PumpMain() noexcept;
    void FillBufferedAudio(int requestedBytes) noexcept;

    AudioMixer* m_mixer = nullptr;
    AudioListenerState* m_listener = nullptr;
    const AudioClipRegistry* m_clips = nullptr;
    AudioVoicePool* m_voices = nullptr;
    AudioMixerState* m_buses = nullptr;
    AudioCommandQueue* m_commands = nullptr;
    AudioStatsBoard* m_stats = nullptr;
    std::recursive_mutex* m_renderLock = nullptr;
    SDL_AudioStream* m_stream = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    int m_targetQueuedBytes = 0;
    std::vector<float> m_chunkScratch;
    bool m_audioSubsystemReady = false;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIODEVICE_H
