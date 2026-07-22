#ifndef CONCORD_LENSFLAREDESC_H
#define CONCORD_LENSFLAREDESC_H

namespace Concord::Effects {

/**
 * Authoring parameters for the camera lens-flare effect.
 *
 * Lens flare draws aperture "ghost" rings along the line from the on-screen sun
 * through the screen center, plus a soft halo, in the final present pass. The
 * sun's screen position is resolved by the renderer each frame from the active
 * camera and directional sun; this desc only controls whether it is on and how
 * strong it is. Disabled by default so it never alters a scene unless requested.
 */
struct LensFlareDesc {
    /** Whether the lens flare is drawn. */
    bool enabled = true;

    /** Overall brightness multiplier for the ghosts and halo, clamped to [0, 4]. */
    float intensity = 1.0f;
};

} // namespace Concord::Effects

#endif // CONCORD_LENSFLAREDESC_H
