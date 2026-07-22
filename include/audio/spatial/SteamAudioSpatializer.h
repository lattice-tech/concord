#ifndef CONCORD_STEAMAUDIOSPATIALIZER_H
#define CONCORD_STEAMAUDIOSPATIALIZER_H

#include "Concord/CExport.h"
#include "audio/spatial/SpatialAudioConfig.h"
#include "audio/spatial/SpatialAudioPose.h"

#include <memory>
#include <span>

namespace Concord::Audio {

/**
 * @brief Converts fixed-size mono frames to HRTF-spatialized stereo frames.
 *
 * Init() creates the Steam Audio context, built-in HRTF, effect, and all work
 * buffers. Process() performs no allocation or locking and is intended to be
 * called serially by one audio thread. Stereo output is interleaved L/R.
 */
class CAUDIO_API SteamAudioSpatializer {
public:
    SteamAudioSpatializer();
    ~SteamAudioSpatializer();

    SteamAudioSpatializer(const SteamAudioSpatializer&) = delete;
    SteamAudioSpatializer& operator=(const SteamAudioSpatializer&) = delete;
    SteamAudioSpatializer(SteamAudioSpatializer&& other) noexcept;
    SteamAudioSpatializer& operator=(SteamAudioSpatializer&& other) noexcept;

    /** @brief Initializes Steam Audio for one fixed processing format. */
    bool Init(const SpatialAudioConfig& config = {});

    /** @brief Releases all DSP resources; safe to call more than once. */
    void Shutdown() noexcept;

    /**
     * @brief Spatializes exactly FrameSize() mono samples into interleaved stereo.
     * @return false for invalid spans, poses, gain, or an uninitialized instance.
     */
    bool Process(std::span<const float> monoInput,
                 std::span<float> stereoOutput,
                 const SpatialAudioListener& listener,
                 const SpatialAudioSource& source) noexcept;

    bool IsInitialized() const noexcept;
    std::int32_t SampleRate() const noexcept;
    std::int32_t FrameSize() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Concord::Audio

#endif // CONCORD_STEAMAUDIOSPATIALIZER_H
