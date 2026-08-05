#ifndef CONCORD_AFFINE_H
#define CONCORD_AFFINE_H

#include "math/Vector3.h"

#include <cmath>

namespace Concord {

/**
 * Column-major 4x4 affine helpers, matching the engine's world matrices
 * (translation at indices 12..14, transformed as `M * p`).
 *
 * Header-only and dependency-free (no bx), so scene-side code can map points
 * between world and local space without pulling the render layer in.
 */

/** Transforms a point (w = 1) by @p m. */
inline Vector3 AffineTransformPoint(const float m[16], const Vector3& p) noexcept
{
    return Vector3{
        p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
        p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
        p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14],
    };
}

/** Transforms a direction (w = 0) by @p m; scale still applies. */
inline Vector3 AffineTransformDirection(const float m[16], const Vector3& d) noexcept
{
    return Vector3{
        d.x * m[0] + d.y * m[4] + d.z * m[8],
        d.x * m[1] + d.y * m[5] + d.z * m[9],
        d.x * m[2] + d.y * m[6] + d.z * m[10],
    };
}

/**
 * @brief Inverts an affine transform (linear 3x3 basis plus translation).
 *
 * Handles rotation, non-uniform scale and shear; the bottom row is assumed to be
 * (0, 0, 0, 1), which every engine world matrix satisfies.
 *
 * @return false for a non-finite or singular matrix, leaving @p out untouched,
 *         so callers fail closed instead of propagating NaNs through a scene.
 */
inline bool AffineInvert(const float m[16], float out[16]) noexcept
{
    if (m == nullptr || out == nullptr) {
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) {
            return false;
        }
    }

    const float a = m[0], b = m[4], c = m[8];
    const float d = m[1], e = m[5], f = m[9];
    const float g = m[2], h = m[6], i = m[10];
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g)
        + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-12f) {
        return false;
    }
    const float inverseDeterminant = 1.0f / determinant;

    // Inverse of the 3x3 basis, written back in column-major order.
    out[0] = (e * i - f * h) * inverseDeterminant;
    out[1] = (c * h - b * i) * inverseDeterminant;
    out[2] = (b * f - c * e) * inverseDeterminant;
    out[3] = 0.0f;
    out[4] = (f * g - d * i) * inverseDeterminant;
    out[5] = (a * i - c * g) * inverseDeterminant;
    out[6] = (c * d - a * f) * inverseDeterminant;
    out[7] = 0.0f;
    out[8] = (d * h - e * g) * inverseDeterminant;
    out[9] = (b * g - a * h) * inverseDeterminant;
    out[10] = (a * e - b * d) * inverseDeterminant;
    out[11] = 0.0f;

    // Inverse translation: -(basis^-1) * translation. The translation of the
    // inverse is -A^-1 * t, and A^-1 is written row-major into out, so the
    // dot products below read out's *rows* (out[0..2], out[4..6], out[8..10]).
    const Vector3 translation{m[12], m[13], m[14]};
    out[12] = -(out[0] * translation.x + out[1] * translation.y + out[2] * translation.z);
    out[13] = -(out[4] * translation.x + out[5] * translation.y + out[6] * translation.z);
    out[14] = -(out[8] * translation.x + out[9] * translation.y + out[10] * translation.z);
    out[15] = 1.0f;
    return true;
}

} // namespace Concord

#endif // CONCORD_AFFINE_H
