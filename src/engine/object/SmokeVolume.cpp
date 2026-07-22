#include "engine/object/SmokeVolume.h"

#include <algorithm>
#include <cmath>

namespace Concord::Object {

namespace {

/** Transforms a local point by the column-major world matrix (point, w=1). */
Vector3 TransformPoint(const float* m, const Vector3& p) noexcept
{
    return {
        p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
        p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
        p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14],
    };
}

} // namespace

SmokeVolume::SmokeVolume(Gameplay::SmokeVolumeDesc desc)
    : m_halfExtents(desc.halfExtents)
    , m_color(desc.color)
    , m_density(desc.density > 0.0f ? desc.density : 0.0f)
    , m_shape(desc.shape)
    , m_noiseScale(desc.noiseScale > 0.01f ? desc.noiseScale : 0.01f)
    , m_coverage(std::clamp(desc.coverage, 0.0f, 1.0f))
    , m_detail(std::clamp(desc.detail, 0.0f, 1.0f))
    , m_edgeSoftness(std::clamp(desc.edgeSoftness, 0.0f, 1.0f))
    , m_anisotropy(std::clamp(desc.anisotropy, -1.0f, 1.0f))
    , m_emissive(desc.emissive > 0.0f ? desc.emissive : 0.0f)
    , m_windVelocity(desc.windVelocity)
    , m_buoyancy(desc.buoyancy)
    , m_animationSpeed(desc.animationSpeed)
{
    SetLocalTransform(desc.transform);
    OnUpdate([this](float deltaTime) { Advance(deltaTime); });
}

void SmokeVolume::SetDensity(float density) noexcept
{
    m_density = density > 0.0f ? density : 0.0f;
}

void SmokeVolume::SetNoiseScale(float scale) noexcept
{
    m_noiseScale = scale > 0.01f ? scale : 0.01f;
}

void SmokeVolume::SetCoverage(float coverage) noexcept
{
    m_coverage = std::clamp(coverage, 0.0f, 1.0f);
}

void SmokeVolume::SetDetail(float detail) noexcept
{
    m_detail = std::clamp(detail, 0.0f, 1.0f);
}

void SmokeVolume::SetEdgeSoftness(float edgeSoftness) noexcept
{
    m_edgeSoftness = std::clamp(edgeSoftness, 0.0f, 1.0f);
}

void SmokeVolume::SetAnisotropy(float anisotropy) noexcept
{
    m_anisotropy = std::clamp(anisotropy, -1.0f, 1.0f);
}

void SmokeVolume::SetEmissive(float emissive) noexcept
{
    m_emissive = emissive > 0.0f ? emissive : 0.0f;
}

void SmokeVolume::Advance(float deltaTime)
{
    // The node owns the animation clock: accumulate scaled time so the density
    // field scrolls (wind) and rises (buoyancy) on its own every frame.
    m_time += deltaTime * m_animationSpeed;
}

void SmokeVolume::CollectSmokeVolumes(std::vector<RenderSmokeVolume>& out) const
{
    const float* world = WorldMatrix();

    // World-space AABB of the local box: transform all eight corners and take
    // the component-wise min/max. A rotated node yields the enclosing
    // axis-aligned box, which the density field is sampled within.
    const Vector3 he = m_halfExtents;
    bool initialized = false;
    Vector3 boxMin{};
    Vector3 boxMax{};
    for (int corner = 0; corner < 8; ++corner) {
        const Vector3 local{
            (corner & 1) ? he.x : -he.x,
            (corner & 2) ? he.y : -he.y,
            (corner & 4) ? he.z : -he.z,
        };
        const Vector3 wp = TransformPoint(world, local);
        if (!initialized) {
            boxMin = boxMax = wp;
            initialized = true;
        } else {
            boxMin.x = std::min(boxMin.x, wp.x);
            boxMin.y = std::min(boxMin.y, wp.y);
            boxMin.z = std::min(boxMin.z, wp.z);
            boxMax.x = std::max(boxMax.x, wp.x);
            boxMax.y = std::max(boxMax.y, wp.y);
            boxMax.z = std::max(boxMax.z, wp.z);
        }
    }

    RenderSmokeVolume volume;
    volume.boxMin[0] = boxMin.x;
    volume.boxMin[1] = boxMin.y;
    volume.boxMin[2] = boxMin.z;
    volume.boxMax[0] = boxMax.x;
    volume.boxMax[1] = boxMax.y;
    volume.boxMax[2] = boxMax.z;
    // Accumulated world scroll of the density field: horizontal wind plus an
    // upward rise. Sampling noise at (worldPos - windOffset) drifts it.
    volume.windOffset[0] = m_windVelocity.x * m_time;
    volume.windOffset[1] = (m_windVelocity.y + m_buoyancy) * m_time;
    volume.windOffset[2] = m_windVelocity.z * m_time;
    volume.color = m_color;
    volume.density = m_density;
    volume.anisotropy = m_anisotropy;
    volume.noiseScale = m_noiseScale;
    volume.coverage = m_coverage;
    volume.detail = m_detail;
    volume.edgeSoftness = m_edgeSoftness;
    volume.emissive = m_emissive;
    volume.shape = m_shape;
    out.push_back(volume);
}

} // namespace Concord::Object
