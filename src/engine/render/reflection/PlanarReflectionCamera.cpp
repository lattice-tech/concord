#include "engine/render/reflection/PlanarReflection.h"

#include <bx/math.h>

#include <cmath>
#include <cstring>

namespace Concord {

bool PlanarReflection::BuildMirrorCamera(const float mainView[16], const float mainProj[16],
                                         const Plane& plane,
                                         float outView[16], float outProj[16],
                                         float outClipPlane[4])
{
    if (!plane.valid) {
        return false;
    }
    float n[3] = {plane.normal[0], plane.normal[1], plane.normal[2]};
    const float nLen = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nLen < 1e-5f) {
        return false;
    }
    n[0] /= nLen;
    n[1] /= nLen;
    n[2] /= nLen;
    float d = -(n[0] * plane.point[0] + n[1] * plane.point[1] + n[2] * plane.point[2]);

    float invMainView[16];
    bx::mtxInverse(invMainView, mainView);
    const float eyeDistance = n[0] * invMainView[12]
        + n[1] * invMainView[13] + n[2] * invMainView[14] + d;
    if (eyeDistance < 0.0f) {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
        d = -d;
    }

    float reflectMtx[16]{};
    reflectMtx[0] = 1.0f - 2.0f * n[0] * n[0];
    reflectMtx[1] = -2.0f * n[0] * n[1];
    reflectMtx[2] = -2.0f * n[0] * n[2];
    reflectMtx[4] = -2.0f * n[1] * n[0];
    reflectMtx[5] = 1.0f - 2.0f * n[1] * n[1];
    reflectMtx[6] = -2.0f * n[1] * n[2];
    reflectMtx[8] = -2.0f * n[2] * n[0];
    reflectMtx[9] = -2.0f * n[2] * n[1];
    reflectMtx[10] = 1.0f - 2.0f * n[2] * n[2];
    reflectMtx[12] = -2.0f * d * n[0];
    reflectMtx[13] = -2.0f * d * n[1];
    reflectMtx[14] = -2.0f * d * n[2];
    reflectMtx[15] = 1.0f;

    bx::mtxMul(outView, reflectMtx, mainView);
    std::memcpy(outProj, mainProj, sizeof(float) * 16);

    outClipPlane[0] = n[0];
    outClipPlane[1] = n[1];
    outClipPlane[2] = n[2];
    outClipPlane[3] = d + 1e-3f;
    return true;
}

} // namespace Concord
