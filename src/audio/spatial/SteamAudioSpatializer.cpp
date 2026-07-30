#include "audio/spatial/SteamAudioSpatializer.h"

#include <phonon.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Audio {
namespace {

bool IsFinite(const Vector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

float Dot(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

bool Normalize(const Vector3& value, Vector3& result) noexcept
{
    if (!IsFinite(value)) {
        return false;
    }
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
        return false;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    result = value * inverseLength;
    return IsFinite(result);
}

} // namespace

class SteamAudioSpatializer::Impl {
public:
    ~Impl() { Shutdown(); }

    bool Init(const SpatialAudioConfig& requested)
    {
        Shutdown();
        if (requested.sampleRate < 8000 || requested.sampleRate > 384000
            || requested.frameSize <= 0 || requested.frameSize > 16384) {
            return false;
        }

        IPLContextSettings contextSettings{};
        contextSettings.version = STEAMAUDIO_VERSION;
        contextSettings.simdLevel = IPL_SIMDLEVEL_SSE2;
        contextSettings.flags = requested.validation
            ? IPL_CONTEXTFLAGS_VALIDATION
            : static_cast<IPLContextFlags>(0);
        if (iplContextCreate(&contextSettings, &m_context) != IPL_STATUS_SUCCESS) {
            return false;
        }

        m_audioSettings.samplingRate = requested.sampleRate;
        m_audioSettings.frameSize = requested.frameSize;
        IPLHRTFSettings hrtfSettings{};
        hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
        hrtfSettings.volume = 1.0f;
        // RMS-normalized HRTF set: consistent loudness across directions, no
        // piercing peaks from the raw default set.
        hrtfSettings.normType = IPL_HRTFNORMTYPE_RMS;
        if (iplHRTFCreate(m_context, &m_audioSettings, &hrtfSettings, &m_hrtf)
            != IPL_STATUS_SUCCESS) {
            Shutdown();
            return false;
        }

        IPLBinauralEffectSettings effectSettings{};
        effectSettings.hrtf = m_hrtf;
        if (iplBinauralEffectCreate(m_context, &m_audioSettings, &effectSettings,
                                    &m_effect) != IPL_STATUS_SUCCESS
            || iplAudioBufferAllocate(m_context, 1, requested.frameSize, &m_input)
                != IPL_STATUS_SUCCESS
            || iplAudioBufferAllocate(m_context, 2, requested.frameSize, &m_output)
                != IPL_STATUS_SUCCESS) {
            Shutdown();
            return false;
        }

        m_config = requested;
        m_initialized = true;
        return true;
    }

    void Shutdown() noexcept
    {
        m_initialized = false;
        if (m_context && m_output.data) {
            iplAudioBufferFree(m_context, &m_output);
        }
        if (m_context && m_input.data) {
            iplAudioBufferFree(m_context, &m_input);
        }
        if (m_effect) {
            iplBinauralEffectRelease(&m_effect);
        }
        if (m_hrtf) {
            iplHRTFRelease(&m_hrtf);
        }
        if (m_context) {
            iplContextRelease(&m_context);
        }
        m_input = {};
        m_output = {};
        m_audioSettings = {};
        m_config = {};
    }

    bool Process(std::span<const float> monoInput,
                 std::span<float> stereoOutput,
                 const SpatialAudioListener& listener,
                 const SpatialAudioSource& source) noexcept
    {
        const std::size_t frameSize = static_cast<std::size_t>(m_config.frameSize);
        if (!m_initialized || monoInput.size() != frameSize
            || stereoOutput.size() != frameSize * 2u
            || !IsFinite(listener.position) || !IsFinite(source.position)
            || !std::isfinite(source.gain) || source.gain < 0.0f
            || !std::isfinite(source.spatialBlend)) {
            return false;
        }

        Vector3 right;
        Vector3 up;
        Vector3 forward;
        Vector3 worldDirection;
        if (!Normalize(listener.right, right) || !Normalize(listener.up, up)
            || !Normalize(listener.forward, forward)
            || !Normalize(source.position - listener.position, worldDirection)) {
            return false;
        }

        Vector3 steamDirection{
            Dot(worldDirection, right),
            Dot(worldDirection, up),
            -Dot(worldDirection, forward)};
        if (!Normalize(steamDirection, steamDirection)) {
            return false;
        }

        for (std::size_t i = 0; i < frameSize; ++i) {
            m_input.data[0][i] = monoInput[i] * source.gain;
        }

        IPLBinauralEffectParams params{};
        params.direction = IPLVector3{
            steamDirection.x, steamDirection.y, steamDirection.z};
        // Bilinear HRTF interpolation: a moving source glides instead of
        // buzzing as it crosses between nearest HRTF measurements.
        params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        params.spatialBlend = std::clamp(source.spatialBlend, 0.0f, 1.0f);
        params.hrtf = m_hrtf;
        iplBinauralEffectApply(m_effect, &params, &m_input, &m_output);

        // A single NaN/Inf from the effect would poison the mix bus for the
        // whole block, so reject the output and let the mixer's pan fallback
        // keep the voice audible instead.
        for (std::size_t i = 0; i < frameSize; ++i) {
            const float left = m_output.data[0][i];
            const float right = m_output.data[1][i];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                return false;
            }
            stereoOutput[i * 2u] = left;
            stereoOutput[i * 2u + 1u] = right;
        }
        return true;
    }

    bool IsInitialized() const noexcept { return m_initialized; }
    std::int32_t SampleRate() const noexcept
    {
        return m_initialized ? m_config.sampleRate : 0;
    }

    std::int32_t FrameSize() const noexcept
    {
        return m_initialized ? m_config.frameSize : 0;
    }

private:
    SpatialAudioConfig m_config{};
    IPLAudioSettings m_audioSettings{};
    IPLContext m_context = nullptr;
    IPLHRTF m_hrtf = nullptr;
    IPLBinauralEffect m_effect = nullptr;
    IPLAudioBuffer m_input{};
    IPLAudioBuffer m_output{};
    bool m_initialized = false;
};

SteamAudioSpatializer::SteamAudioSpatializer() = default;
SteamAudioSpatializer::~SteamAudioSpatializer() = default;
SteamAudioSpatializer::SteamAudioSpatializer(SteamAudioSpatializer&& other) noexcept = default;
SteamAudioSpatializer& SteamAudioSpatializer::operator=(SteamAudioSpatializer&& other) noexcept = default;

bool SteamAudioSpatializer::Init(const SpatialAudioConfig& config)
{
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }
    return m_impl->Init(config);
}

void SteamAudioSpatializer::Shutdown() noexcept
{
    if (m_impl) {
        m_impl->Shutdown();
    }
}

bool SteamAudioSpatializer::Process(std::span<const float> monoInput,
                                    std::span<float> stereoOutput,
                                    const SpatialAudioListener& listener,
                                    const SpatialAudioSource& source) noexcept
{
    return m_impl && m_impl->Process(monoInput, stereoOutput, listener, source);
}

bool SteamAudioSpatializer::IsInitialized() const noexcept
{
    return m_impl && m_impl->IsInitialized();
}

std::int32_t SteamAudioSpatializer::SampleRate() const noexcept
{
    return m_impl ? m_impl->SampleRate() : 0;
}

std::int32_t SteamAudioSpatializer::FrameSize() const noexcept
{
    return m_impl ? m_impl->FrameSize() : 0;
}

} // namespace Concord::Audio
