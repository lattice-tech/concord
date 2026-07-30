#include "engine/object/water/WaterBody.h"

#include "math/Affine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Concord::Object {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

float SafePositive(float value, float fallback) noexcept
{
    return (std::isfinite(value) && value > 0.0f) ? value : fallback;
}

} // namespace

WaterBody::WaterBody(Water::WaterSurfaceDesc desc)
    : m_desc(desc)
{
    m_desc.width = SafePositive(desc.width, 1.0f);
    m_desc.length = SafePositive(desc.length, 1.0f);
    m_desc.depth = SafePositive(desc.depth, 1.0f);
    m_desc.tessellation = std::clamp(desc.tessellation, 1u, 512u);
    m_desc.waveCount = std::min(desc.waveCount, Water::kMaxWaterWaves);
    m_desc.absorption = std::max(desc.absorption, 0.0f);
    m_desc.roughness = std::clamp(desc.roughness, 0.001f, 1.0f);
    m_desc.refractionStrength = std::clamp(desc.refractionStrength, 0.0f, 1.0f);
    m_desc.foamIntensity = std::clamp(desc.foamIntensity, 0.0f, 1.0f);
    m_desc.foamWidth = std::max(desc.foamWidth, 0.0f);
    SetLocalTransform(m_desc.transform);
    ResolveRuntimeState();
    RebuildWaves();
    OnUpdate([this](float deltaTime) { Advance(deltaTime); });
}

void WaterBody::RebuildWaves()
{
    // A still surface is exactly flat rather than "almost flat": that is what
    // lets it hold a clean mirror reflection.
    if (m_desc.motion == Water::WaterMotion::Still) {
        m_waves = Water::WaveSet{};
        return;
    }
    if (m_state.waveSource == Water::WaterWaveSource::WindSpectrum) {
        m_waves = Water::BuildWaveSetFromWind(m_state.wind);
        // A spectrum expand that found nothing to bake falls back to the plain
        // Gerstner set rather than rendering a flat ocean on a windy day.
        if (m_waves.count > 0) {
            return;
        }
    }
    m_waves = Water::BuildWaveSet(m_desc.waveCount, m_desc.waveAmplitude,
                                  m_desc.waveLength, m_desc.waveSteepness,
                                  m_desc.waveSpeed, m_desc.waveDirectionDegrees);
}

Water::WaterWaveSource WaterBody::ResolveWaveSource() const noexcept
{
    if (m_desc.motion == Water::WaterMotion::Still) {
        return Water::WaterWaveSource::Flat;
    }
    if (std::isfinite(m_state.wind.windSpeed) && m_state.wind.windSpeed > 0.01f) {
        return Water::WaterWaveSource::WindSpectrum;
    }
    return Water::WaterWaveSource::Gerstner;
}

void WaterBody::ResolveRuntimeState() noexcept
{
    m_state.wind = m_desc.wind;
    m_state.optics = m_desc.optics;
    m_state.waveSource = ResolveWaveSource();
}

void WaterBody::Advance(float deltaTime)
{
    if (m_desc.motion == Water::WaterMotion::Still || !std::isfinite(deltaTime)) {
        return;
    }
    m_state.time += deltaTime;
    if (m_desc.flowSpeed != 0.0f && std::isfinite(m_desc.flowSpeed)) {
        const float heading = m_desc.flowDirectionDegrees * kDegToRad;
        m_state.flowOffsetX += std::cos(heading) * m_desc.flowSpeed * deltaTime;
        m_state.flowOffsetZ += std::sin(heading) * m_desc.flowSpeed * deltaTime;
    }
}

void WaterBody::ToLocalXZ(float worldX, float worldZ, float& outX, float& outZ) const
{
    outX = worldX;
    outZ = worldZ;
    float inverse[16];
    if (!AffineInvert(WorldMatrix(), inverse)) {
        return;
    }
    // Sampling is a plane query, so the probe rides the surface plane (local
    // y = 0 mapped back to world) instead of the caller's own height.
    const Vector3 planeOrigin =
        AffineTransformPoint(WorldMatrix(), Vector3{0.0f, 0.0f, 0.0f});
    const Vector3 local =
        AffineTransformPoint(inverse, Vector3{worldX, planeOrigin.y, worldZ});
    outX = local.x;
    outZ = local.z;
}

Water::SurfacePoint WaterBody::SurfacePoint(float worldX, float worldZ) const
{
    float localX = 0.0f;
    float localZ = 0.0f;
    ToLocalXZ(worldX, worldZ, localX, localZ);
    const Water::SurfacePoint local =
        Water::SampleSurface(m_waves, localX, localZ, m_state.time);

    Water::SurfacePoint world;
    world.position = AffineTransformPoint(WorldMatrix(), local.position);
    const Vector3 normal = AffineTransformDirection(WorldMatrix(), local.normal);
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y
                                   + normal.z * normal.z);
    world.normal = (length > 1.0e-6f && std::isfinite(length))
        ? Vector3{normal.x / length, normal.y / length, normal.z / length}
        : Vector3{0.0f, 1.0f, 0.0f};
    return world;
}

float WaterBody::SurfaceHeight(float worldX, float worldZ) const
{
    return SurfacePoint(worldX, worldZ).position.y;
}

bool WaterBody::ContainsPoint(float worldX, float worldZ) const
{
    float localX = 0.0f;
    float localZ = 0.0f;
    ToLocalXZ(worldX, worldZ, localX, localZ);
    return std::fabs(localX) <= m_desc.width * 0.5f
        && std::fabs(localZ) <= m_desc.length * 0.5f;
}

bool WaterBody::IsSubmerged(const Vector3& worldPosition) const
{
    if (!ContainsPoint(worldPosition.x, worldPosition.z)) {
        return false;
    }
    return worldPosition.y < SurfaceHeight(worldPosition.x, worldPosition.z);
}

void WaterBody::SetWaves(std::uint32_t count, float amplitude, float wavelength,
                         float steepness, float speed, float directionDegrees)
{
    m_desc.waveCount = std::min(count, Water::kMaxWaterWaves);
    m_desc.waveAmplitude = amplitude;
    m_desc.waveLength = wavelength;
    m_desc.waveSteepness = steepness;
    m_desc.waveSpeed = speed;
    m_desc.waveDirectionDegrees = directionDegrees;
    // Explicit Gerstner authoring overrides the wind path until wind is set again.
    m_state.wind.windSpeed = 0.0f;
    m_desc.wind.windSpeed = 0.0f;
    m_state.waveSource = ResolveWaveSource();
    RebuildWaves();
}

void WaterBody::SetFlow(float speed, float directionDegrees)
{
    m_desc.flowSpeed = std::isfinite(speed) ? speed : 0.0f;
    m_desc.flowDirectionDegrees =
        std::isfinite(directionDegrees) ? directionDegrees : 0.0f;
}

void WaterBody::SetMotion(Water::WaterMotion motion) noexcept
{
    if (m_desc.motion == motion) {
        return;
    }
    m_desc.motion = motion;
    m_state.waveSource = ResolveWaveSource();
    RebuildWaves();
}

void WaterBody::SetColors(std::uint32_t shallowColor, std::uint32_t deepColor) noexcept
{
    m_desc.shallowColor = shallowColor;
    m_desc.deepColor = deepColor;
}

void WaterBody::SetDepth(float depth) noexcept
{
    m_desc.depth = SafePositive(depth, m_desc.depth);
}

void WaterBody::SetFoam(float width, float intensity) noexcept
{
    m_desc.foamWidth = std::max(width, 0.0f);
    m_desc.foamIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

void WaterBody::SetWind(const Water::WindState& wind) noexcept
{
    m_desc.wind = wind;
    ResolveRuntimeState();
    RebuildWaves();
}

void WaterBody::SetOptics(const Water::WaterOptics& optics) noexcept
{
    m_desc.optics = optics;
    m_state.optics = optics;
}

void WaterBody::CollectWaterSurfaces(std::vector<RenderWaterSurface>& out) const
{
    out.push_back(ResolveSurface());
}

RenderWaterSurface WaterBody::ResolveSurface() const
{
    RenderWaterSurface surface;
    std::memcpy(surface.world, WorldMatrix(), sizeof(surface.world));
    surface.width = m_desc.width;
    surface.length = m_desc.length;
    surface.waves = m_waves;
    surface.state = m_state;
    if (m_desc.motion == Water::WaterMotion::Dynamic
        && std::isfinite(m_desc.flowSpeed)) {
        const float heading = m_desc.flowDirectionDegrees * kDegToRad;
        surface.flowVelocity[0] = std::cos(heading) * m_desc.flowSpeed;
        surface.flowVelocity[1] = std::sin(heading) * m_desc.flowSpeed;
    }
    surface.tessellation = m_desc.tessellation;
    surface.depth = m_desc.depth;
    surface.shallowColor = m_desc.shallowColor;
    surface.deepColor = m_desc.deepColor;
    surface.absorption = m_desc.absorption;
    surface.roughness = m_desc.roughness;
    surface.refractionStrength = m_desc.refractionStrength;
    surface.foamWidth = m_desc.foamWidth;
    surface.foamIntensity = m_desc.foamIntensity;
    surface.kind = m_desc.kind;
    surface.motion = m_desc.motion;
    surface.planarReflection = m_desc.planarReflection;
    return surface;
}

} // namespace Concord::Object
