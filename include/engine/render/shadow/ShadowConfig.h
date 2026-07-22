#ifndef CONCORD_SHADOWCONFIG_H
#define CONCORD_SHADOWCONFIG_H

#include <cstdint>

namespace Concord {

inline constexpr std::uint32_t kShadowCascadeCount = 3;

/**
 * Tunables for the single directional-light shadow pass.
 *
 * The values are plain data the render backend reads when it builds the shadow
 * frustum, configures its depth-compare uniforms for `fs_mesh`, and offsets the
 * surface point using its geometric normal before projecting into light clip
 * space. Picked to keep shadow acne off lit faces while avoiding the
 * "peter-panning" lift-off seen with too-large bias constants; tweak here once
 * instead of hunting them through every caller that asks for shadows.
 */
struct ShadowConfig {
    /** Square shadow-map edge length for each cascade. */
    std::uint32_t resolution = 2048;

    /** Practical split weight: 0 is uniform, 1 is logarithmic. */
    float cascadeSplitLambda = 0.65f;

    /** Fraction of each cascade depth interval blended into the next cascade. */
    float cascadeBlendFraction = 0.1f;

    /**
     * Maximum upstream caster distance retained behind a cascade's receivers.
     * Keeping this fixed prevents moving objects from changing the projection
     * used by otherwise static shadows.
     */
    float casterExtrusionWorld = 64.0f;

    /**
     * Constant light-clip-space depth bias added under the fragment's depth
     * before the (point-sampled) shadow comparison, on top of a clamped
     * receiver-plane slope term computed in the shader. Kept small so contact
     * shadows stay attached; the normal offset below does the heavy lifting.
     */
    float depthBias = 0.0004f;

    /**
     * Receiver normal offset measured in shadow-map texels. Each cascade
     * converts this to world units from its orthographic width and resolution.
     */
    float normalBiasTexels = 1.0f;

    /** Blocker-search half-width in shadow-map texels for PCSS. */
    float blockerSearchRadiusTexels = 10.0f;

    /** Minimum PCF half-width in texels, slightly softened to hide silhouette steps. */
    float minFilterRadiusTexels = 1.5f;

    /**
     * Maximum PCF half-width in texels. Kept moderate so distant penumbrae
     * do not form large circular soft-shadow "blobs" on walls.
     */
    float maxFilterRadiusTexels = 8.0f;
};

} // namespace Concord

#endif // CONCORD_SHADOWCONFIG_H
