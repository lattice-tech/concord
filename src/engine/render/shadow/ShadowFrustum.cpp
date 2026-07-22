#include "engine/render/shadow/ShadowFrustum.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace Concord {

namespace {

/** Rounds a positive span up to a power of two so moving bounds do not resize continuously. */
float QuantizeSpan(float span) noexcept
{
    span = std::max(span, 1.0f);
    return std::exp2(std::ceil(std::log2(span)));
}

/** Fits depth bounds to a texel-aligned power-of-two span. */
void FitStableDepthBounds(float minimum, float maximum, float slack,
                          std::uint32_t resolution, float& nearPlane,
                          float& farPlane) noexcept
{
    const float coverageResolution = static_cast<float>(std::max(resolution, 2u));
    const float resolutionScale = coverageResolution / (coverageResolution - 1.0f);
    const float required = (maximum - minimum + 2.0f * slack) * resolutionScale;
    const float extent = QuantizeSpan(required);
    const float texel = extent / static_cast<float>(std::max(resolution, 1u));
    const float center = std::floor((minimum + maximum) * 0.5f / texel + 0.5f)
        * texel;
    nearPlane = center - extent * 0.5f;
    farPlane = center + extent * 0.5f;
    if (nearPlane < 0.01f) {
        farPlane += 0.01f - nearPlane;
        nearPlane = 0.01f;
    }
}

/** Normalizes `v` with a straight-down fallback if it is degenerate. */
bx::Vec3 Normalize(const bx::Vec3& v) noexcept
{
    const float len = bx::length(v);
    if (len <= 1e-6f) {
        return {0.0f, -1.0f, 0.0f};
    }
    return bx::mul(v, 1.0f / len);
}

/**
 * Transforms a world-space point by the row-major view matrix (row-vector
 * convention, translation at indices 12/13/14).
 */
bx::Vec3 TransformPoint(const float* m, const bx::Vec3& p) noexcept
{
    return {
        p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12],
        p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13],
        p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14],
    };
}

/** Expands `vmin`/`vmax` to include `p`. */
void ExpandAabb(bx::Vec3& vmin, bx::Vec3& vmax, const bx::Vec3& p) noexcept
{
    vmin = bx::min(vmin, p);
    vmax = bx::max(vmax, p);
}

} // namespace

void ComputeShadowFrustum(const float lightDir[3],
                          const float aabbMin[3],
                          const float aabbMax[3],
                          std::uint32_t resolution,
                          bool homogeneousDepth,
                          ShadowFrustumResult& out)
{
    const bx::Vec3 dir = Normalize({lightDir[0], lightDir[1], lightDir[2]});

    // Choose an "up" axis that is not parallel to the light direction so the
    // look-at basis stays well-conditioned (no NaN-filled basis).
    const bool vertical = dir.y > 0.99f || dir.y < -0.99f;
    const bx::Vec3 worldUp = vertical ? bx::Vec3{0.0f, 0.0f, 1.0f} : bx::Vec3{0.0f, 1.0f, 0.0f};

    // Move the eye only along the light direction. Tracking the AABB in the two
    // transverse axes would move the light-space basis every frame and defeat
    // texel snapping whenever a dynamic caster changed the scene bounds.
    const bx::Vec3 aabbCenter = bx::mul(bx::add(
        bx::Vec3{aabbMin[0], aabbMin[1], aabbMin[2]},
        bx::Vec3{aabbMax[0], aabbMax[1], aabbMax[2]}), 0.5f);
    const bx::Vec3 aabbHalf = bx::mul(bx::sub(
        bx::Vec3{aabbMax[0], aabbMax[1], aabbMax[2]},
        bx::Vec3{aabbMin[0], aabbMin[1], aabbMin[2]}), 0.5f);
    const float eyeDistance = bx::length(aabbHalf) + 100.0f;
    const float alongLight = bx::dot(aabbCenter, dir);
    const bx::Vec3 eye = bx::mul(dir, alongLight - eyeDistance);
    const bx::Vec3 target = bx::add(eye, dir);

    bx::mtxLookAt(out.viewMatrix, eye, target, worldUp);

    // Fit the orthographic frustum tightly around the scene's world AABB (the
    // shadow casters and the ground they fall on). Transform all 8 corners into
    // light view space and take their bounds. Fitting the scene — rather than
    // the camera frustum, whose 100-unit far plane would blow the ortho up to
    // ~200 units and leave each object only a handful of texels — keeps texel
    // density high (≈100 texels/unit at 4096 over a 40-unit scene), which is
    // what makes the shadows read as crisp rather than a faint smear.
    bx::Vec3 viewMin{ 1e30f,  1e30f,  1e30f};
    bx::Vec3 viewMax{-1e30f, -1e30f, -1e30f};

    const bx::Vec3 aabbCorners[8] = {
        {aabbMin[0], aabbMin[1], aabbMin[2]},
        {aabbMax[0], aabbMin[1], aabbMin[2]},
        {aabbMin[0], aabbMax[1], aabbMin[2]},
        {aabbMax[0], aabbMax[1], aabbMin[2]},
        {aabbMin[0], aabbMin[1], aabbMax[2]},
        {aabbMax[0], aabbMin[1], aabbMax[2]},
        {aabbMin[0], aabbMax[1], aabbMax[2]},
        {aabbMax[0], aabbMax[1], aabbMax[2]},
    };
    for (const bx::Vec3& c : aabbCorners) {
        ExpandAabb(viewMin, viewMax, TransformPoint(out.viewMatrix, c));
    }

    const float slack = 0.5f;
    const float resolutionScale = static_cast<float>(std::max(resolution, 2u))
        / static_cast<float>(std::max(resolution, 2u) - 1u);
    const float requiredWidth = (viewMax.x - viewMin.x + 2.0f * slack) * resolutionScale;
    const float requiredHeight = (viewMax.y - viewMin.y + 2.0f * slack) * resolutionScale;
    // A square power-of-two coverage changes only when the scene crosses a
    // coarse boundary. The previous tight fit changed world-units-per-texel on
    // every movement and made all shadow edges shimmer.
    const float extent = QuantizeSpan(std::max(requiredWidth, requiredHeight));
    float left = viewMin.x - slack;
    float right = viewMax.x + slack;
    float bottom = viewMin.y - slack;
    float top = viewMax.y + slack;
    float nearP;
    float farP;
    FitStableDepthBounds(viewMin.z, viewMax.z, slack, resolution, nearP, farP);

    const float width = extent;
    const float height = extent;

    // Keep the light projection aligned to shadow-map texels. Without this,
    // sub-texel changes in the fitted scene bounds move every projected edge
    // through the map and make otherwise static shadows shimmer. Dynamic
    // casters are still submitted into a freshly rendered map every frame; the
    // snap stabilizes the projection, not the geometry in it.
    const float texelX = width / static_cast<float>(std::max(resolution, 1u));
    const float texelY = height / static_cast<float>(std::max(resolution, 1u));
    const float centerX = std::floor(((left + right) * 0.5f) / texelX + 0.5f) * texelX;
    const float centerY = std::floor(((bottom + top) * 0.5f) / texelY + 0.5f) * texelY;
    left = centerX - width * 0.5f;
    right = centerX + width * 0.5f;
    bottom = centerY - height * 0.5f;
    top = centerY + height * 0.5f;

    out.orthoWidth = width;
    out.depthRange = std::max(farP - nearP, 0.01f);
    bx::mtxOrtho(out.projMatrix, left, right, bottom, top, nearP, farP, 0.0f, homogeneousDepth);
    bx::mtxMul(out.viewProjMatrix, out.viewMatrix, out.projMatrix);
}

void ComputeCascadeShadowFrustum(const float lightDir[3],
                                 const float receiverCorners[8][3],
                                 float casterExtrusionWorld,
                                 std::uint32_t resolution,
                                 float samplingGuardTexels,
                                 bool homogeneousDepth,
                                 ShadowFrustumResult& out)
{
    const bx::Vec3 dir = Normalize({lightDir[0], lightDir[1], lightDir[2]});
    bx::Vec3 center{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 8; ++i) {
        center = bx::add(center, bx::Vec3{
            receiverCorners[i][0], receiverCorners[i][1], receiverCorners[i][2]});
    }
    center = bx::mul(center, 1.0f / 8.0f);

    float radius = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const bx::Vec3 corner{
            receiverCorners[i][0], receiverCorners[i][1], receiverCorners[i][2]};
        radius = std::max(radius, bx::length(bx::sub(corner, center)));
    }
    // Quantization prevents tiny camera rotations or floating-point drift from
    // continuously changing world-units-per-texel.
    radius = std::ceil(std::max(radius, 0.5f) * 16.0f) / 16.0f;
    const float resolutionF = static_cast<float>(std::max(resolution, 1u));
    const float guard = std::clamp(samplingGuardTexels, 0.0f, resolutionF * 0.25f);
    // Keep the quantized receiver sphere inside a guard measured in final-map
    // texels. Expanding after radius quantization preserves projection stability.
    radius *= resolutionF / std::max(resolutionF - 2.0f * guard, 1.0f);

    const bool vertical = std::abs(dir.y) > 0.99f;
    const bx::Vec3 worldUp = vertical ? bx::Vec3{0.0f, 0.0f, 1.0f} : bx::Vec3{0.0f, 1.0f, 0.0f};
    float receiverMinAlong = 1e30f;
    float receiverMaxAlong = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const bx::Vec3 corner{
            receiverCorners[i][0], receiverCorners[i][1], receiverCorners[i][2]};
        const float along = bx::dot(bx::sub(corner, center), dir);
        receiverMinAlong = std::min(receiverMinAlong, along);
        receiverMaxAlong = std::max(receiverMaxAlong, along);
    }
    const float casterExtrusion = std::isfinite(casterExtrusionWorld)
        ? std::max(casterExtrusionWorld, 0.0f) : 0.0f;
    constexpr float kDepthSlack = 1.0f;
    const float eyeDistance = casterExtrusion - receiverMinAlong + kDepthSlack;
    // Keep transverse light-view coordinates world anchored. Following the
    // receiver center in XY would move the view matrix continuously and defeat
    // projection snapping. Depth depends only on the receiver interval and the
    // fixed caster extrusion, never on moving scene bounds.
    const float alongLight = bx::dot(center, dir);
    const bx::Vec3 eye = bx::mul(dir, alongLight - eyeDistance);
    const bx::Vec3 target = bx::add(eye, dir);
    bx::mtxLookAt(out.viewMatrix, eye, target, worldUp);

    const float extent = radius * 2.0f;
    const float texel = extent / resolutionF;
    const bx::Vec3 centerView = TransformPoint(out.viewMatrix, center);
    const float centerX = std::floor(centerView.x / texel + 0.5f) * texel;
    const float centerY = std::floor(centerView.y / texel + 0.5f) * texel;
    constexpr float nearP = 0.01f;
    const float farP = eyeDistance + receiverMaxAlong + kDepthSlack;

    out.orthoWidth = extent;
    out.depthRange = std::max(farP - nearP, 0.01f);
    bx::mtxOrtho(out.projMatrix,
                 centerX - radius, centerX + radius,
                 centerY - radius, centerY + radius,
                 nearP, farP, 0.0f, homogeneousDepth);
    bx::mtxMul(out.viewProjMatrix, out.viewMatrix, out.projMatrix);
}

} // namespace Concord
