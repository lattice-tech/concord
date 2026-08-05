#include "engine/animation/clip/SkeletalClip.h"

#include "math/MatrixTransform.h"

namespace Concord::Animation {
namespace {

/** Conjugate (inverse for unit quaternions) — flips the imaginary part. */
Quaternion Conjugate(const Quaternion& q) noexcept
{
    return Quaternion{-q.x, -q.y, -q.z, q.w};
}

/**
 * Samples the root bone's world transform at @p time.
 * @return false when the pose or bone cannot be resolved.
 */
bool SampleRootWorld(const SkeletalClip& clip, float time,
                     const Skeleton& skeleton, Transform& out)
{
    SkeletonPose pose;
    clip.Sample(time, skeleton, pose);
    Matrix4 world;
    if (!skeleton.ComputeBoneWorld(pose, clip.rootBone, world)) {
        return false;
    }
    return MatrixToTransform(world, out);
}

/**
 * Motion of the root bone across one non-wrapping segment [from, to].
 */
bool SegmentDelta(const SkeletalClip& clip, float from, float to,
                  const Skeleton& skeleton, Vector3& outPosition,
                  Quaternion& outRotation)
{
    Transform fromWorld;
    Transform toWorld;
    if (!SampleRootWorld(clip, from, skeleton, fromWorld)
        || !SampleRootWorld(clip, to, skeleton, toWorld)) {
        return false;
    }
    outPosition = Vector3{toWorld.position.x - fromWorld.position.x,
                          toWorld.position.y - fromWorld.position.y,
                          toWorld.position.z - fromWorld.position.z};
    outRotation = Conjugate(fromWorld.rotation) * toWorld.rotation;
    return true;
}

} // namespace

bool SkeletalClip::SampleRootMotion(float fromTime, float toTime,
                                    const Skeleton& skeleton,
                                    Vector3& outPositionDelta,
                                    Quaternion& outRotationDelta) const
{
    if (rootBone < 0 || rootBone >= static_cast<int>(skeleton.bones.size())) {
        return false;
    }
    const float duration = Duration();
    if (duration <= 0.0f) {
        return false;
    }
    if (toTime >= fromTime) {
        return SegmentDelta(*this, fromTime, toTime, skeleton, outPositionDelta,
                            outRotationDelta);
    }

    // Loop wrap: tail (from -> duration) then head (0 -> to).
    Vector3 tailPosition;
    Quaternion tailRotation;
    Vector3 headPosition;
    Quaternion headRotation;
    if (!SegmentDelta(*this, fromTime, duration, skeleton, tailPosition, tailRotation)
        || !SegmentDelta(*this, 0.0f, toTime, skeleton, headPosition, headRotation)) {
        return false;
    }
    outPositionDelta = Vector3{tailPosition.x + headPosition.x,
                               tailPosition.y + headPosition.y,
                               tailPosition.z + headPosition.z};
    outRotationDelta = tailRotation * headRotation;
    return true;
}

} // namespace Concord::Animation
