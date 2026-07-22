#include "engine/object/Light.h"

#include <algorithm>
#include <cmath>

namespace Concord::Object {

namespace {

/** Normalizes `v`, falling back to straight down for a degenerate direction. */
Vector3 Normalize(const Vector3& v) noexcept
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-6f) {
        return {0.0f, -1.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

float ClampDirectionalAngularRadius(float radiusDegrees) noexcept
{
    if (!std::isfinite(radiusDegrees)) {
        return kDefaultDirectionalAngularRadiusDegrees;
    }
    return std::clamp(radiusDegrees, 0.0f, 45.0f);
}

/**
 * Rotates the local direction `d` by the node's world matrix (upper 3x3).
 *
 * The world matrix is row-major with a row-vector convention (see Node), so a
 * direction transforms as `d * M`, reading M's basis from its rows. Translation
 * is intentionally ignored: a direction has no position.
 */
Vector3 WorldDirection(const float* m, const Vector3& d) noexcept
{
    return Normalize({
        d.x * m[0] + d.y * m[4] + d.z * m[8],
        d.x * m[1] + d.y * m[5] + d.z * m[9],
        d.x * m[2] + d.y * m[6] + d.z * m[10],
    });
}

} // namespace

Light::Light(LightDesc desc)
    : m_type(desc.type)
    , m_direction(desc.direction)
    , m_color(desc.color)
    , m_intensity(desc.intensity)
    , m_range(desc.range)
    , m_sourceRadius(desc.sourceRadius)
    , m_directionalAngularRadiusDegrees(
          ClampDirectionalAngularRadius(desc.directionalAngularRadiusDegrees))
    , m_innerAngleDegrees(desc.innerAngleDegrees)
    , m_outerAngleDegrees(desc.outerAngleDegrees)
    , m_castShadow(desc.castShadow)
{
    SetLocalTransform(desc.transform);
}

void Light::SetType(LightType type)
{
    m_type = type;
}

void Light::SetDirection(Vector3 direction)
{
    m_direction = direction;
}

void Light::SetColor(std::uint32_t color)
{
    m_color = color;
}

void Light::SetIntensity(float intensity)
{
    m_intensity = intensity;
}

void Light::SetRange(float range)
{
    m_range = range;
}

void Light::SetSourceRadius(float radius) noexcept
{
    m_sourceRadius = radius > 0.0f ? radius : 0.0f;
}

void Light::SetDirectionalAngularRadiusDegrees(float radiusDegrees) noexcept
{
    m_directionalAngularRadiusDegrees = ClampDirectionalAngularRadius(radiusDegrees);
}

void Light::SetConeAngles(float innerDegrees, float outerDegrees)
{
    m_outerAngleDegrees = outerDegrees;
    m_innerAngleDegrees = std::min(innerDegrees, outerDegrees);
}

void Light::SetSunAppearance(bool sun, bool visibleDisk, float diskIntensity) noexcept
{
    m_sun = sun;
    m_visibleDisk = visibleDisk;
    m_visibleDiskIntensity = std::isfinite(diskIntensity)
        ? std::max(diskIntensity, 0.0f) : 1.0f;
}

void Light::CollectLights(std::vector<RenderLight>& out) const
{
    const float* world = WorldMatrix();
    const Vector3 position = WorldPosition();
    const Vector3 direction = WorldDirection(world, m_direction);

    RenderLight light;
    light.type = m_type;
    light.position[0] = position.x;
    light.position[1] = position.y;
    light.position[2] = position.z;
    light.direction[0] = direction.x;
    light.direction[1] = direction.y;
    light.direction[2] = direction.z;
    light.color = m_color;
    light.intensity = m_intensity;
    light.range = m_range;
    light.sourceRadius = m_sourceRadius;
    light.directionalAngularRadiusDegrees = m_directionalAngularRadiusDegrees;
    light.innerAngleDegrees = m_innerAngleDegrees;
    light.outerAngleDegrees = m_outerAngleDegrees;
    light.castShadow = m_castShadow;
    light.sun = m_sun;
    light.visibleDisk = m_visibleDisk;
    light.visibleDiskIntensity = m_visibleDiskIntensity;
    out.push_back(light);
}

} // namespace Concord::Object
