#ifndef CONCORD_LIGHTDESC_H
#define CONCORD_LIGHTDESC_H

#include "color/Color.h"
#include "engine/object/Transform.h"
#include "engine/render/frame/RenderLight.h"

#include <cstdint>

namespace Concord::Object {

/**
 * Every field a Light can be constructed or Set() from.
 *
 * A plain aggregate, like the other *Desc types, so a caller names only the
 * fields it wants, e.g.
 * `LightDesc{.type = LightType::Point, .transform = {.position = {0, 4, 0}}}`.
 *
 * Placement follows the scene graph: a light's world position and the world
 * direction it emits along are both derived from the inherited Node transform,
 * so a light can be SetParent-ed under a moving node and ride along. `direction`
 * here is the light's *local* forward (the axis the cone/rays follow before the
 * node's rotation is applied); the default points straight down. Which of
 * position/direction actually matters depends on `type`:
 *   - Directional: direction only (an infinitely distant sun).
 *   - Point: position only (an omni bulb); `range` bounds its reach.
 *   - Spot: position, direction, `range`, and the two cone angles.
 */
struct LightDesc {
    /** Emission model: directional (default), point, or spot. */
    LightType type = LightType::Directional;

    /** Placement in the scene graph (position + rotation orient the light). */
    Transform transform{};

    /** Local forward the light emits along, before the node's rotation. */
    Vector3 direction{0.0f, -1.0f, 0.0f};

    /** Emitted color, packed 0xRRGGBBAA. */
    std::uint32_t color = COLOR_WHITE;

    /** Radiant scale on `color`. */
    float intensity = 1.0f;

    /** Distance at which a point/spot light fades to zero (world units). */
    float range = 20.0f;

    /**
     * Physical size of a point/spot emitter (world units). Larger values soften
     * near-field 1/r² and reduce hard circular isophotes on walls. Ignored for
     * directional lights. Default ~a small lamp; author freely in game code:
     * `LightDesc{.type = LightType::Point, .sourceRadius = 0.8f, ...}`.
     */
    float sourceRadius = 0.4f;

    /**
     * Apparent angular radius of a directional emitter, in degrees. The
     * physical Sun is approximately 0.2666 degrees. Values are clamped to
     * [0, 45]; zero produces a point-like directional emitter.
     */
    float directionalAngularRadiusDegrees = kDefaultDirectionalAngularRadiusDegrees;

    /** Spot inner cone half-angle in degrees (full intensity within). */
    float innerAngleDegrees = 25.0f;

    /** Spot outer cone half-angle in degrees (zero intensity beyond). */
    float outerAngleDegrees = 35.0f;

    /**
     * Whether this light generates a directional-light shadow map. Only
     * honored for `LightType::Directional`; the render backend uses the first
     * directional light with this set as the frame's single shadow caster.
     */
    bool castShadow = false;
};

} // namespace Concord::Object

#endif // CONCORD_LIGHTDESC_H
