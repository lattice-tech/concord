#ifndef CONCORD_SHADOWFRUSTUM_H
#define CONCORD_SHADOWFRUSTUM_H

#include <cstdint>

namespace Concord {

/**
 * Light-space view, orthographic projection and their product, in the row-major
 * 4x4 layout bx and the rest of the engine use (translation at indices 12/13/14).
 *
 * `ComputeShadowFrustum` fills these from a directional light's world travel
 * direction and the world-space AABB the shadow frustum must cover. The
 * matrices are ready to hand straight to `bgfx::setViewTransform` (view+proj) or
 * to upload as the `u_lightViewProj` mat4 uniform the mesh fragment shader samples.
 */
struct ShadowFrustumResult {
    /** Row-major light view (look-at along the light's travel direction). */
    float viewMatrix[16];

    /** Row-major orthographic projection covering the AABB in light view space. */
    float projMatrix[16];

    /** Row-major `viewMatrix * projMatrix`, ready for upload to `u_lightViewProj`. */
    float viewProjMatrix[16];

    /** Orthographic width in world units, used to derive world units per texel. */
    float orthoWidth = 1.0f;

    /** Light-space near-to-far span in world units, used by PCSS penumbra sizing. */
    float depthRange = 1.0f;
};

/**
 * Builds one stable CSM projection around eight world-space receiver corners.
 * XY coverage uses a rotation-invariant bounding sphere and texel snapping;
 * light-space depth covers the receiver interval plus a fixed upstream caster
 * extrusion, so moving geometry cannot change the projection of static shadows.
 * `samplingGuardTexels` reserves an inset for PCSS taps, receiver bias and
 * center snapping so filter reads never depend on clamped map-edge texels.
 */
void ComputeCascadeShadowFrustum(const float lightDir[3],
                                 const float receiverCorners[8][3],
                                 float casterExtrusionWorld,
                                 std::uint32_t resolution,
                                 float samplingGuardTexels,
                                 bool homogeneousDepth,
                                 ShadowFrustumResult& out);

/**
 * Builds a shadow-caster frustum that fits `aabbMin`/`aabbMax` for a light
 * travelling along `lightDir`.
 *
 * @param lightDir Unit world-space direction the light travels along.
 * @param aabbMin  Minimum corner of the world AABB the frustum must cover
 *                 (shadow casters — ensures off-screen objects still cast).
 * @param aabbMax  Maximum corner of the world AABB.
 * @param resolution Shadow-map edge length, used to snap the projection to
 *                   texel increments and prevent sub-texel swimming.
 * @param homogeneousDepth `bgfx::getCaps()->homogeneousDepth`.
 * @param out      Receives the matrices and physical projection spans.
 */
void ComputeShadowFrustum(const float lightDir[3],
                          const float aabbMin[3],
                          const float aabbMax[3],
                          std::uint32_t resolution,
                          bool homogeneousDepth,
                          ShadowFrustumResult& out);

} // namespace Concord

#endif // CONCORD_SHADOWFRUSTUM_H
