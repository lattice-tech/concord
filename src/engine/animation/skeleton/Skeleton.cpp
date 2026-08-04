#include "engine/animation/Skeleton.h"

namespace Concord::Animation {

int Skeleton::BoneIndex(const std::string& name) const
{
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SkeletonPose Skeleton::BindPose() const
{
    SkeletonPose pose;
    pose.local.reserve(bones.size());
    for (const Bone& bone : bones) {
        pose.local.push_back(bone.bindLocal);
    }
    return pose;
}

namespace {

/**
 * Fills global[i] (model-space transform of bone i) resolving parents first.
 * `done` memoises so each bone is composed once regardless of array order or
 * how deep the hierarchy is; a parent stored after its child is handled by the
 * recursive dependency walk. A malformed parent cycle is broken by the `done`
 * guard (a bone already in progress is treated as resolved at identity).
 */
void ResolveGlobal(std::size_t i, const std::vector<Bone>& bones,
                   const SkeletonPose& pose, std::vector<Matrix4>& global,
                   std::vector<char>& done, const Matrix4& rootTransform)
{
    if (done[i]) {
        return;
    }
    done[i] = 1; // mark before recursing to break any accidental cycle
    const Matrix4 localMtx = Matrix4::FromTransform(pose.local[i]);
    const int parent = bones[i].parent;
    if (parent < 0 || parent >= static_cast<int>(bones.size())) {
        // Root bone: apply the skeleton's fixed pre-transform (non-joint
        // ancestors) in front of its animated local transform.
        global[i] = Matrix4::Multiply(rootTransform, localMtx);
    } else {
        ResolveGlobal(static_cast<std::size_t>(parent), bones, pose, global, done, rootTransform);
        global[i] = Matrix4::Multiply(global[static_cast<std::size_t>(parent)], localMtx);
    }
}

} // namespace

void Skeleton::ComputePalette(const SkeletonPose& pose, std::vector<Matrix4>& outPalette) const
{
    const std::size_t count = bones.size();
    outPalette.resize(count);
    if (count == 0 || pose.local.size() != count) {
        return;
    }

    std::vector<Matrix4> global(count);
    std::vector<char> done(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        ResolveGlobal(i, bones, pose, global, done, rootTransform);
    }
    for (std::size_t i = 0; i < count; ++i) {
        outPalette[i] = Matrix4::Multiply(global[i], bones[i].inverseBind);
    }
}

} // namespace Concord::Animation
