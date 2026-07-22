#ifndef CONCORD_MATRIX4_H
#define CONCORD_MATRIX4_H

#include "engine/object/Transform.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

namespace Concord {

/**
 * A 4x4 transform matrix stored **column-major**, matching the convention the
 * mesh vertex shader consumes (it blends the four columns explicitly:
 * `col0*x + col1*y + col2*z + col3`). Element `m[col*4 + row]`.
 *
 * Header-only and dependency-free (no bx), so it can be used in animation
 * headers and unit-tested on its own. Used chiefly to build the per-bone
 * skinning palette: compose each bone's animated local transform up the
 * hierarchy, then multiply by its inverse bind matrix.
 */
struct Matrix4 {
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    static Matrix4 Identity() noexcept { return Matrix4{}; }

    /** Builds a column-major TRS matrix (scale, then rotate, then translate). */
    static Matrix4 FromTransform(const Transform& t) noexcept
    {
        // Rotation 3x3 from the quaternion.
        const float x = t.rotation.x, y = t.rotation.y, z = t.rotation.z, w = t.rotation.w;
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;

        const float r00 = 1.0f - 2.0f * (yy + zz);
        const float r01 = 2.0f * (xy - wz);
        const float r02 = 2.0f * (xz + wy);
        const float r10 = 2.0f * (xy + wz);
        const float r11 = 1.0f - 2.0f * (xx + zz);
        const float r12 = 2.0f * (yz - wx);
        const float r20 = 2.0f * (xz - wy);
        const float r21 = 2.0f * (yz + wx);
        const float r22 = 1.0f - 2.0f * (xx + yy);

        const float sx = t.scale.x, sy = t.scale.y, sz = t.scale.z;

        Matrix4 out;
        // Column 0 = rotation column 0 * sx.
        out.m[0] = r00 * sx; out.m[1] = r10 * sx; out.m[2] = r20 * sx; out.m[3] = 0.0f;
        // Column 1 = rotation column 1 * sy.
        out.m[4] = r01 * sy; out.m[5] = r11 * sy; out.m[6] = r21 * sy; out.m[7] = 0.0f;
        // Column 2 = rotation column 2 * sz.
        out.m[8] = r02 * sz; out.m[9] = r12 * sz; out.m[10] = r22 * sz; out.m[11] = 0.0f;
        // Column 3 = translation.
        out.m[12] = t.position.x; out.m[13] = t.position.y; out.m[14] = t.position.z; out.m[15] = 1.0f;
        return out;
    }

    /** Column-major matrix product `a * b` (applies b first, then a). */
    static Matrix4 Multiply(const Matrix4& a, const Matrix4& b) noexcept
    {
        Matrix4 out;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                out.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return out;
    }
};

} // namespace Concord

#endif // CONCORD_MATRIX4_H
