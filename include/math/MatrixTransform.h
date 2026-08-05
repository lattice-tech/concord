#ifndef CONCORD_MATRIXTRANSFORM_H
#define CONCORD_MATRIXTRANSFORM_H

#include "engine/object/Transform.h"
#include "math/Matrix4.h"

#include <cmath>

namespace Concord {

/**
 * @brief Decomposes a column-major matrix into TRS.
 *
 * The bottom row must be (0,0,0,1); the scale is read from the column lengths
 * and the rotation from the normalised basis (Shepperd's method). This is the
 * inverse of Matrix4::FromTransform — a transform baked into a matrix can be
 * read back out (e.g. a bone's world pose for an attachment).
 *
 * @return false for a singular basis, leaving @p out untouched.
 */
inline bool MatrixToTransform(const Matrix4& m, Transform& out) noexcept
{
    const float sx = std::sqrt(m.m[0] * m.m[0] + m.m[1] * m.m[1] + m.m[2] * m.m[2]);
    const float sy = std::sqrt(m.m[4] * m.m[4] + m.m[5] * m.m[5] + m.m[6] * m.m[6]);
    const float sz = std::sqrt(m.m[8] * m.m[8] + m.m[9] * m.m[9] + m.m[10] * m.m[10]);
    if (sx < 1.0e-8f || sy < 1.0e-8f || sz < 1.0e-8f) {
        return false;
    }

    // Normalised rotation basis, row-major naming (m[col*4+row]).
    const float n00 = m.m[0] / sx, n01 = m.m[4] / sy, n02 = m.m[8] / sz;
    const float n10 = m.m[1] / sx, n11 = m.m[5] / sy, n12 = m.m[9] / sz;
    const float n20 = m.m[2] / sx, n21 = m.m[6] / sy, n22 = m.m[10] / sz;

    float qw = 1.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    const float trace = n00 + n11 + n22;
    if (trace > 0.0f) {
        const float s = 2.0f * std::sqrt(trace + 1.0f);
        qw = 0.25f * s;
        qx = (n21 - n12) / s;
        qy = (n02 - n20) / s;
        qz = (n10 - n01) / s;
    } else if (n00 >= n11 && n00 >= n22) {
        const float s = 2.0f * std::sqrt(1.0f + n00 - n11 - n22);
        qw = (n21 - n12) / s;
        qx = 0.25f * s;
        qy = (n01 + n10) / s;
        qz = (n02 + n20) / s;
    } else if (n11 >= n22) {
        const float s = 2.0f * std::sqrt(1.0f + n11 - n00 - n22);
        qw = (n02 - n20) / s;
        qx = (n01 + n10) / s;
        qy = 0.25f * s;
        qz = (n12 + n21) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + n22 - n00 - n11);
        qw = (n10 - n01) / s;
        qx = (n02 + n20) / s;
        qy = (n12 + n21) / s;
        qz = 0.25f * s;
    }
    const float len = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
    if (len < 1.0e-8f) {
        return false;
    }

    out.position = Vector3{m.m[12], m.m[13], m.m[14]};
    out.rotation = Quaternion{qx / len, qy / len, qz / len, qw / len};
    out.scale = Vector3{sx, sy, sz};
    return true;
}

} // namespace Concord

#endif // CONCORD_MATRIXTRANSFORM_H
