#include "engine/spatial/Frustum.h"

#include "engine/collision/AabbOps.h"

#include <algorithm>
#include <cmath>

namespace Concord::Spatial {
namespace {

void NormalizePlane(Frustum::Plane& plane) noexcept
{
    const float length = std::sqrt(plane.a * plane.a + plane.b * plane.b + plane.c * plane.c);
    if (length <= 1.0e-8f || !std::isfinite(length)) {
        plane = {};
        return;
    }
    const float inv = 1.0f / length;
    plane.a *= inv;
    plane.b *= inv;
    plane.c *= inv;
    plane.d *= inv;
}

void MultiplyMatrix(const float left[16], const float right[16], float out[16]) noexcept
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            out[column * 4 + row] =
                left[0 * 4 + row] * right[column * 4 + 0]
                + left[1 * 4 + row] * right[column * 4 + 1]
                + left[2 * 4 + row] * right[column * 4 + 2]
                + left[3 * 4 + row] * right[column * 4 + 3];
        }
    }
}

/**
 * Rebuilds the projection the render backend will use for this camera.
 *
 * It must match `bx::mtxProj` / `bx::mtxOrtho` exactly, including bx's default
 * **left-handed** basis (view space looks down +Z, the same basis
 * Camera::GetCameraView bakes with bx::mtxLookAt) and the `[0,1]` clip depth
 * range Vulkan uses (Concord is Vulkan only, so `homogeneousDepth` is always
 * false). Getting the handedness wrong does not merely loosen culling: it
 * extracts the mirrored frustum behind the camera, which keeps whatever sits
 * near the eye and drops geometry as soon as the camera tilts away from it.
 */
void BuildProjection(const CameraView& camera, float aspect, float out[16]) noexcept
{
    const float safeAspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float nearPlane = camera.nearPlane > 0.0f ? camera.nearPlane : 0.1f;
    const float farPlane = camera.farPlane > nearPlane ? camera.farPlane : nearPlane + 1.0f;
    const float depth = farPlane - nearPlane;
    for (int i = 0; i < 16; ++i) {
        out[i] = 0.0f;
    }

    if (camera.projection == Projection::Orthographic) {
        const float halfHeight = camera.orthoHeight > 0.0f ? camera.orthoHeight * 0.5f : 5.0f;
        const float halfWidth = halfHeight * safeAspect;
        out[0] = 1.0f / halfWidth;
        out[5] = 1.0f / halfHeight;
        out[10] = 1.0f / depth;
        out[14] = -nearPlane / depth;
        out[15] = 1.0f;
        return;
    }

    const float fov = camera.fovYDegrees > 0.0f ? camera.fovYDegrees : 60.0f;
    const float f = 1.0f / std::tan(fov * 0.5f * 0.01745329251994329577f);
    out[0] = f / safeAspect;
    out[5] = f;
    out[10] = farPlane / depth;
    out[11] = 1.0f;
    out[14] = -(nearPlane * farPlane) / depth;
}

} // namespace

Frustum BuildFrustum(const CameraView& camera, float aspect) noexcept
{
    float projection[16]{};
    BuildProjection(camera, aspect, projection);
    float clip[16]{};
    MultiplyMatrix(projection, camera.viewMatrix, clip);

    Frustum frustum;
    // Column-major clip matrix → Gribb/Hartmann planes. The clip volume is
    // -w <= x,y <= w with 0 <= z <= w (Vulkan depth), so the near plane is the
    // z row on its own rather than w+z.
    frustum.planes[0] = {clip[3] + clip[0], clip[7] + clip[4], clip[11] + clip[8],
                         clip[15] + clip[12]}; // left
    frustum.planes[1] = {clip[3] - clip[0], clip[7] - clip[4], clip[11] - clip[8],
                         clip[15] - clip[12]}; // right
    frustum.planes[2] = {clip[3] + clip[1], clip[7] + clip[5], clip[11] + clip[9],
                         clip[15] + clip[13]}; // bottom
    frustum.planes[3] = {clip[3] - clip[1], clip[7] - clip[5], clip[11] - clip[9],
                         clip[15] - clip[13]}; // top
    frustum.planes[4] = {clip[2], clip[6], clip[10], clip[14]}; // near
    frustum.planes[5] = {clip[3] - clip[2], clip[7] - clip[6], clip[11] - clip[10],
                         clip[15] - clip[14]}; // far
    for (Frustum::Plane& plane : frustum.planes) {
        NormalizePlane(plane);
    }
    return frustum;
}

bool FrustumIntersectsAabb(const Frustum& frustum, const Collision::Aabb& box) noexcept
{
    if (!Collision::IsValidAabb(box)) {
        return false;
    }
    for (const Frustum::Plane& plane : frustum.planes) {
        const float px = plane.a >= 0.0f ? box.max.x : box.min.x;
        const float py = plane.b >= 0.0f ? box.max.y : box.min.y;
        const float pz = plane.c >= 0.0f ? box.max.z : box.min.z;
        if (plane.a * px + plane.b * py + plane.c * pz + plane.d < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace Concord::Spatial
