#include "engine/effects/view/ScreenEffectStack.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

constexpr std::size_t kMaxConcurrentShakes = 32;
constexpr float kMaxShakeAmplitudePixels = 2048.0f;
constexpr float kMaxCombinedShakePixels = 4096.0f;
constexpr float kMaxShakeDuration = 3600.0f;
constexpr float kMaxShakeFrequency = 240.0f;
constexpr float kMaxShakeDecay = 8.0f;
constexpr float kMaxMagnifierRadius = 2.0f;
constexpr float kMaxMagnifierZoom = 16.0f;
constexpr float kMaxMagnifierDistortion = 2.0f;

float FiniteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::clamp(FiniteOr(value, fallback), minimum, maximum);
}

std::uint32_t HashNoise(std::uint32_t seed, std::uint32_t sample, std::uint32_t axis) noexcept
{
    std::uint32_t value = seed ^ (sample * 0x9E3779B9u) ^ (axis * 0x85EBCA6Bu);
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

float NoiseValue(std::uint32_t seed, std::uint32_t sample, std::uint32_t axis) noexcept
{
    constexpr float kScale = 1.0f / 4294967295.0f;
    return static_cast<float>(HashNoise(seed, sample, axis)) * kScale * 2.0f - 1.0f;
}

float SmoothNoise(std::uint32_t seed, float samplePosition, std::uint32_t axis) noexcept
{
    const float clampedPosition = std::max(samplePosition, 0.0f);
    const auto first = static_cast<std::uint32_t>(std::floor(clampedPosition));
    const float fraction = clampedPosition - static_cast<float>(first);
    const float blend = fraction * fraction * (3.0f - 2.0f * fraction);
    const float a = NoiseValue(seed, first, axis);
    const float b = NoiseValue(seed, first + 1u, axis);
    return a + (b - a) * blend;
}

Concord::Effects::ScreenShakeDesc SanitizeShake(
    const Concord::Effects::ScreenShakeDesc& desc) noexcept
{
    Concord::Effects::ScreenShakeDesc sanitized = desc;
    sanitized.amplitudePixels.x = ClampFinite(
        std::fabs(desc.amplitudePixels.x), 0.0f, kMaxShakeAmplitudePixels, 0.0f);
    sanitized.amplitudePixels.y = ClampFinite(
        std::fabs(desc.amplitudePixels.y), 0.0f, kMaxShakeAmplitudePixels, 0.0f);
    sanitized.duration = ClampFinite(desc.duration, 0.0f, kMaxShakeDuration, 0.0f);
    sanitized.frequency = ClampFinite(desc.frequency, 0.0f, kMaxShakeFrequency, 0.0f);
    sanitized.decay = ClampFinite(desc.decay, 0.0f, kMaxShakeDecay, 1.0f);
    return sanitized;
}

Concord::Effects::MagnifierEffectDesc SanitizeMagnifier(
    const Concord::Effects::MagnifierEffectDesc& desc) noexcept
{
    Concord::Effects::MagnifierEffectDesc sanitized = desc;
    sanitized.center.x = ClampFinite(desc.center.x, 0.0f, 1.0f, 0.5f);
    sanitized.center.y = ClampFinite(desc.center.y, 0.0f, 1.0f, 0.5f);
    sanitized.radius = ClampFinite(desc.radius, 0.0f, kMaxMagnifierRadius, 0.25f);
    sanitized.zoom = ClampFinite(desc.zoom, 1.0f, kMaxMagnifierZoom, 1.0f);
    sanitized.distortion = ClampFinite(
        desc.distortion, -kMaxMagnifierDistortion, kMaxMagnifierDistortion, 0.0f);
    sanitized.feather = ClampFinite(desc.feather, 0.0f, sanitized.radius, 0.0f);
    sanitized.enabled = desc.enabled && sanitized.radius > 0.0f;
    return sanitized;
}

} // namespace

namespace Concord::Effects {

ScreenEffectStack::ScreenEffectStack()
{
    ApplyMagnifierState();
}

ScreenEffectStack::~ScreenEffectStack() = default;

bool ScreenEffectStack::PlayShake(const ScreenShakeDesc& desc)
{
    const ScreenShakeDesc sanitized = SanitizeShake(desc);
    if (sanitized.duration <= 0.0f
        || (sanitized.amplitudePixels.x <= 0.0f && sanitized.amplitudePixels.y <= 0.0f)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shakes.size() >= kMaxConcurrentShakes) {
        m_shakes.erase(m_shakes.begin());
    }
    m_shakes.push_back(ShakeInstance{sanitized, 0.0f});
    RebuildShakeState();
    return true;
}

void ScreenEffectStack::ClearShakes()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shakes.clear();
    m_state.shakeOffsetPixels[0] = 0.0f;
    m_state.shakeOffsetPixels[1] = 0.0f;
}

bool ScreenEffectStack::IsShaking() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_shakes.empty();
}

Vector2 ScreenEffectStack::ShakeOffsetPixels() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_state.shakeOffsetPixels[0], m_state.shakeOffsetPixels[1]};
}

void ScreenEffectStack::Advance(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (ShakeInstance& shake : m_shakes) {
        shake.elapsed = std::min(shake.elapsed + deltaTime, shake.desc.duration);
    }
    std::erase_if(m_shakes, [](const ShakeInstance& shake) {
        return shake.elapsed >= shake.desc.duration;
    });
    RebuildShakeState();
}

void ScreenEffectStack::SetMagnifier(const MagnifierEffectDesc& desc)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_magnifier = SanitizeMagnifier(desc);
    ApplyMagnifierState();
}

void ScreenEffectStack::DisableMagnifier()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_magnifier.enabled = false;
    ApplyMagnifierState();
}

MagnifierEffectDesc ScreenEffectStack::Magnifier() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_magnifier;
}

void ScreenEffectStack::SetLensFlare(const LensFlareDesc& desc)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lensFlare.enabled = desc.enabled;
    m_lensFlare.intensity = ClampFinite(desc.intensity, 0.0f, 4.0f, 1.0f);
    ApplyLensFlareState();
}

void ScreenEffectStack::DisableLensFlare()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lensFlare.enabled = false;
    ApplyLensFlareState();
}

LensFlareDesc ScreenEffectStack::LensFlare() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lensFlare;
}

void ScreenEffectStack::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shakes.clear();
    m_magnifier = MagnifierEffectDesc{};
    m_lensFlare = LensFlareDesc{.enabled = false, .intensity = 1.0f};
    m_state = ViewEffectState{};
    ApplyMagnifierState();
    ApplyLensFlareState();
}

ViewEffectState ScreenEffectStack::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void ScreenEffectStack::RebuildShakeState()
{
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    for (const ShakeInstance& shake : m_shakes) {
        const float progress = std::clamp(shake.elapsed / shake.desc.duration, 0.0f, 1.0f);
        const float remaining = 1.0f - progress;
        const float envelope = shake.desc.decay > 0.0f
            ? std::pow(remaining, shake.desc.decay)
            : 1.0f;
        const float samplePosition = shake.elapsed * shake.desc.frequency;
        offsetX += SmoothNoise(shake.desc.seed, samplePosition, 0u)
            * shake.desc.amplitudePixels.x * envelope;
        offsetY += SmoothNoise(shake.desc.seed, samplePosition, 1u)
            * shake.desc.amplitudePixels.y * envelope;
    }

    m_state.shakeOffsetPixels[0] = std::clamp(
        offsetX, -kMaxCombinedShakePixels, kMaxCombinedShakePixels);
    m_state.shakeOffsetPixels[1] = std::clamp(
        offsetY, -kMaxCombinedShakePixels, kMaxCombinedShakePixels);
}

void ScreenEffectStack::ApplyMagnifierState()
{
    m_state.magnifierCenter[0] = m_magnifier.center.x;
    m_state.magnifierCenter[1] = m_magnifier.center.y;
    m_state.magnifierRadius = m_magnifier.radius;
    m_state.magnifierZoom = m_magnifier.zoom;
    m_state.magnifierDistortion = m_magnifier.distortion;
    m_state.magnifierFeather = m_magnifier.feather;
    m_state.magnifierEnabled = m_magnifier.enabled ? 1u : 0u;
}

void ScreenEffectStack::ApplyLensFlareState()
{
    m_state.lensFlareEnabled = m_lensFlare.enabled ? 1u : 0u;
    m_state.lensFlareIntensity = m_lensFlare.intensity;
    // The sun screen position is filled by the renderer each frame; leave it.
}

} // namespace Concord::Effects
