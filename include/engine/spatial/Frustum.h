#ifndef CONCORD_FRUSTUM_H
#define CONCORD_FRUSTUM_H

#include "engine/collision/Aabb.h"
#include "engine/render/frame/CameraView.h"
#include "math/Vector3.h"

#include <array>
#include <cmath>

namespace Concord::Spatial {

/**
 * @brief Six half-spaces of a view frustum in world space.
 *
 * Planes use the form ax+by+cz+d >= 0 for points inside. Extracted from a
 * column-major view * projection matrix so orthographic and perspective cameras
 * share one path.
 */
struct Frustum {
    struct Plane {
        float a = 0.0f;
        float b = 0.0f;
        float c = 0.0f;
        float d = 0.0f;
    };

    std::array<Plane, 6> planes{};
};

/**
 * Builds a world-space frustum from a CameraView and the target aspect ratio.
 * Aspect is width/height; non-positive values fall back to 1.
 */
Frustum BuildFrustum(const CameraView& camera, float aspect) noexcept;

/**
 * True when @p box is not completely outside every plane (touching counts as
 * visible). Invalid boxes are treated as culled.
 */
bool FrustumIntersectsAabb(const Frustum& frustum, const Collision::Aabb& box) noexcept;

} // namespace Concord::Spatial

#endif // CONCORD_FRUSTUM_H
