#ifndef CONCORD_SMOKEVOLUME_H
#define CONCORD_SMOKEVOLUME_H

#include "Concord/CExport.h"
#include "engine/object/Node.h"
#include "engine/object/SmokeVolumeDesc.h"
#include "engine/render/frame/RenderSmokeVolume.h"
#include "math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Concord::Object {

/**
 * A local volume of participating smoke: a first-class scene node the smoke
 * pass ray-marches and composites against scene depth.
 *
 * Created through Scene::Spawn<SmokeVolume>(desc). Its world-space bounding box
 * is derived from the inherited Node transform each frame, so moving or
 * reparenting the volume (or an ancestor) moves the smoke next frame with no
 * explicit push — exactly like Light does for illumination. The Scene gathers
 * every active volume into the frame's smoke list and hands it to the render
 * thread, which packs the boxes into the smoke shader's uniform arrays.
 *
 * The node owns the animation clock: each frame it advances an internal time by
 * `animationSpeed * dt` and derives the density field's world scroll from
 * `windVelocity` plus upward `buoyancy`, so the smoke drifts and roils on its
 * own. Gameplay can retune every parameter at runtime through the setters
 * (e.g. thicken the smoke, change the wind, freeze it) without respawning.
 */
class CENGINE_API SmokeVolume : public Node {
public:
    explicit SmokeVolume(Gameplay::SmokeVolumeDesc desc = {});

    const Vector3& HalfExtents() const noexcept { return m_halfExtents; }
    std::uint32_t Color() const noexcept { return m_color; }
    float Density() const noexcept { return m_density; }
    SmokeShape Shape() const noexcept { return m_shape; }
    float Coverage() const noexcept { return m_coverage; }
    const Vector3& WindVelocity() const noexcept { return m_windVelocity; }
    float Buoyancy() const noexcept { return m_buoyancy; }
    float AnimationSpeed() const noexcept { return m_animationSpeed; }

    /** Sets the local half-size of the region (before the node's transform scale). */
    void SetHalfExtents(Vector3 halfExtents) noexcept { m_halfExtents = halfExtents; }

    /** Sets the smoke tint, packed 0xRRGGBBAA (sRGB). */
    void SetColor(std::uint32_t color) noexcept { m_color = color; }

    /** Sets the optical density (extinction scale); clamped to >= 0. */
    void SetDensity(float density) noexcept;

    /** Selects the soft boundary shape (box or rounded ellipsoid). */
    void SetShape(SmokeShape shape) noexcept { m_shape = shape; }

    /** Sets the base-noise cell size in world units (clamped to > 0). */
    void SetNoiseScale(float scale) noexcept;

    /** Sets the fill fraction, 0..1 (higher fills more of the region). */
    void SetCoverage(float coverage) noexcept;

    /** Sets the high-frequency erosion strength, 0..1. */
    void SetDetail(float detail) noexcept;

    /** Sets the boundary fade fraction, 0..1. */
    void SetEdgeSoftness(float edgeSoftness) noexcept;

    /** Sets the Henyey-Greenstein anisotropy in [-1, 1]. */
    void SetAnisotropy(float anisotropy) noexcept;

    /** Sets the self-emission scale (>= 0); adds glow before absorption. */
    void SetEmissive(float emissive) noexcept;

    /** Sets the world-space drift velocity of the density field (units/second). */
    void SetWindVelocity(Vector3 velocity) noexcept { m_windVelocity = velocity; }

    /** Sets the upward drift speed (units/second); makes the smoke rise. */
    void SetBuoyancy(float buoyancy) noexcept { m_buoyancy = buoyancy; }

    /** Sets the animation-rate multiplier (0 freezes the field). */
    void SetAnimationSpeed(float speed) noexcept { m_animationSpeed = speed; }

private:
    void Advance(float deltaTime);
    void CollectSmokeVolumes(std::vector<RenderSmokeVolume>& out) const override;

    Vector3 m_halfExtents;
    std::uint32_t m_color;
    float m_density;
    SmokeShape m_shape;
    float m_noiseScale;
    float m_coverage;
    float m_detail;
    float m_edgeSoftness;
    float m_anisotropy;
    float m_emissive;
    Vector3 m_windVelocity;
    float m_buoyancy;
    float m_animationSpeed;

    /** Accumulated animation time (seconds * animationSpeed); drives the scroll. */
    float m_time = 0.0f;
};

} // namespace Concord::Object

#endif // CONCORD_SMOKEVOLUME_H
