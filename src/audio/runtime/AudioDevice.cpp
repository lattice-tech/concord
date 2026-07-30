#include "audio/runtime/detail/AudioDevice.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <thread>

namespace Concord::Audio::Detail {

using Clock = std::chrono::steady_clock;

bool AudioDevice::Init(const AudioRuntimeConfig& config, AudioMixer& mixer,
                       const AudioListenerState* listener, const AudioClipRegistry* clips,
                       AudioVoicePool* voices, const AudioMixerState* buses,
                       AudioStats* stats, std::recursive_mutex* renderLock)
{
    Shutdown();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    m_audioSubsystemReady = true;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = config.device.sampleRate;
    m_mixer = &mixer;
    m_listener = listener;
    m_clips = clips;
    m_voices = voices;
    m_buses = buses;
    m_stats = stats;
    m_renderLock = renderLock;
    m_chunkScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_targetQueuedBytes = std::max(1, config.device.bufferedFrames)
        * static_cast<int>(sizeof(float) * 2);
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, nullptr, nullptr);
    if (m_stream == nullptr) {
        std::fprintf(stderr, "[audio] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        Shutdown();
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        std::fprintf(stderr, "[audio] SDL_ResumeAudioStreamDevice failed: %s\n", SDL_GetError());
        Shutdown();
        return false;
    }
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&AudioDevice::PumpMain, this);
    return true;
}

void AudioDevice::Pump() noexcept
{
    if (m_stream == nullptr) {
        return;
    }
    const int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes >= 0 && queuedBytes < m_targetQueuedBytes) {
        FillBufferedAudio(m_targetQueuedBytes - queuedBytes);
    }
}

void AudioDevice::Shutdown() noexcept
{
    m_running.store(false, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_stream != nullptr) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    if (m_audioSubsystemReady) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_audioSubsystemReady = false;
    }
    m_mixer = nullptr;
    m_listener = nullptr;
    m_clips = nullptr;
    m_voices = nullptr;
    m_buses = nullptr;
    m_stats = nullptr;
    m_renderLock = nullptr;
    m_targetQueuedBytes = 0;
    m_chunkScratch.clear();
}

void AudioDevice::PumpMain() noexcept
{
    while (m_running.load(std::memory_order_acquire)) {
        if (m_stream == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        const int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
        if (queuedBytes < 0) {
            ++m_stats->underrunCount;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (queuedBytes < m_targetQueuedBytes) {
            FillBufferedAudio(m_targetQueuedBytes - queuedBytes);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void AudioDevice::FillBufferedAudio(int requestedBytes) noexcept
{
    if (m_mixer == nullptr || m_listener == nullptr || m_clips == nullptr
        || m_voices == nullptr || m_buses == nullptr || m_stats == nullptr
        || m_renderLock == nullptr || m_stream == nullptr || requestedBytes <= 0) {
        return;
    }
    // Held for the whole fill so the main thread cannot mutate voices, clips,
    // the listener or bus state mid-mix (torn reads audible as static).
    std::lock_guard<std::recursive_mutex> lock(*m_renderLock);
    // Always render whole fixed-size blocks: the HRTF spatializer's overlap
    // history assumes exactly frameSize frames per call, so a short tail block
    // would inject zero-padded silence into its convolution every fill and be
    // audible as periodic clicks. Overfilling by less than one block is fine —
    // the extra simply raises the queue slightly above target.
    const int chunkBytes = static_cast<int>(m_chunkScratch.size() * sizeof(float));
    const std::uint32_t chunkFrames = static_cast<std::uint32_t>(m_chunkScratch.size() / 2u);
    if (chunkFrames == 0) {
        return;
    }
    const auto start = Clock::now();
    int remaining = requestedBytes;
    while (remaining > 0) {
        m_mixer->Render(m_chunkScratch.data(), chunkFrames, *m_listener,
                        *m_clips, *m_voices, *m_buses, *m_stats);
        if (!SDL_PutAudioStreamData(m_stream, m_chunkScratch.data(), chunkBytes)) {
            ++m_stats->underrunCount;
            std::fprintf(stderr, "[audio] SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
            break;
        }
        remaining -= chunkBytes;
    }
    m_stats->callbackCpuMs = std::chrono::duration<float, std::milli>(
        Clock::now() - start).count();
}

} // namespace Concord::Audio::Detail
