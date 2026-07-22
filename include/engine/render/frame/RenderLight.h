#ifndef CONCORD_RENDERLIGHT_H
#define CONCORD_RENDERLIGHT_H

#include <cstdint>

namespace Concord {

/** How a light emits: parallel rays, an omni point, or a cone. */
enum class LightType : std::uint8_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

/**
 * The maximum number of lights the mesh shader accumulates in one draw.
 *
 * Kept in lock-step with `MAX_LIGHTS` in `fs_mesh.sc`: the backend uploads at
 * most this many lights per view, and lights beyond it are dropped with a
 * diagnostic (see Requirement 2.7). Raising it means editing both places and
 * rebuilding the shaders.
 */
inline constexpr std::uint32_t kMaxRenderLights = 8;

/** Physical angular radius of the Sun as seen from Earth, in degrees. */
inline constexpr float kDefaultDirectionalAngularRadiusDegrees = 0.2666f;

/**
 * The render thread's flat, resolved form of one scene light.
 *
 * Object::Light produces the rich, caller-facing description; this is the
 * backend-agnostic POD the Scene gathers each frame (see Scene::Tick) and the
 * render thread packs into the shading pass's uniforms. Like RenderInstance /
 * RenderMaterial it carries no identity or ownership — just "this light, this
 * frame". Directions and positions are world-space, already derived from the
 * light node's world transform. Colors are packed 0xRRGGBBAA (sRGB), unpacked
 * and linearized by the backend/shader at upload time.
 */
struct RenderLight {
    /** Which emission model this light uses. */
    LightType type = LightType::Directional;

    /** World-space position (point/spot only; ignored for directional). */
    float position[3]{0.0f, 0.0f, 0.0f};

    /**
     * World-space unit direction the light travels along (directional/spot).
     * The shading pass negates this to get the surface-to-light vector.
     */
    float direction[3]{0.0f, -1.0f, 0.0f};

    /** Emitted color, packed 0xRRGGBBAA (sRGB); alpha is ignored. */
    std::uint32_t color = 0xffffffffu;

    /** Radiant scale applied to `color`; watts-ish, not physically calibrated. */
    float intensity = 1.0f;

    /**
     * Distance (world units) at which a point/spot light's contribution
     * smoothly reaches zero. Ignored for directional lights.
     */
    float range = 20.0f;

    /**
     * Emitter radius for point/spot (world units). Softens attenuation near the
     * source; packed into the shading pass as `u_lightSpot.z`.
     */
    float sourceRadius = 0.4f;

    /**
     * Apparent angular radius of a directional emitter, in degrees. The
     * shadow pass uses it to derive contact-hardening penumbra growth. Point
     * and spot lights ignore this field.
     */
    float directionalAngularRadiusDegrees = kDefaultDirectionalAngularRadiusDegrees;

    /** Spot inner cone half-angle in degrees: full intensity inside this. */
    float innerAngleDegrees = 25.0f;

    /** Spot outer cone half-angle in degrees: zero intensity outside this. */
    float outerAngleDegrees = 35.0f;

    /**
     * Whether this light participates in the directional-light shadow pass.
     * Only meaningful for `LightType::Directional`; point/spot lights never
     * cast a shadow in the current engine. The render backend picks the first
     * directional light with this flag set as the frame's shadow caster.
     */
    bool castShadow = false;

    /** Explicit celestial-sun role used by sky, cloud, fog, and shadow selection. */
    bool sun = false;

    /** Whether the environment renderer may draw this light's visible disk. */
    bool visibleDisk = false;

    /** Visible solar-disk luminance multiplier, independent from scene illuminance. */
    float visibleDiskIntensity = 1.0f;
};

} // namespace Concord

#endif // CONCORD_RENDERLIGHT_H
