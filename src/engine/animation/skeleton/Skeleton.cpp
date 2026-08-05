#include "engine/animation/skeleton/Skeleton.h"

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

bool Skeleton::ChainToRoot(int boneIndex, std::vector<int>& out) const
{
    out.clear();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones.size())) {
        return false;
    }
    int current = boneIndex;
    std::size_t guard = bones.size() + 1;
    while (current >= 0) {
        if (guard-- == 0 || current >= static_cast<int>(bones.size())) {
            out.clear();
            return false;
        }
        out.push_back(current);
        current = bones[static_cast<std::size_t>(current)].parent;
    }
    return true;
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

bool ResolveAllGlobals(const std::vector<Bone>& bones, const SkeletonPose& pose,
                       const Matrix4& rootTransform, std::vector<Matrix4>& outGlobal)
{
    const std::size_t count = bones.size();
    if (count == 0 || pose.local.size() != count) {
        return false;
    }
    outGlobal.resize(count);
    std::vector<char> done(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        ResolveGlobal(i, bones, pose, outGlobal, done, rootTransform);
    }
    return true;
}

} // namespace

bool Skeleton::ComputeBoneWorld(const SkeletonPose& pose, int boneIndex,
                                Matrix4& out) const
{
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones.size())) {
        return false;
    }
    std::vector<Matrix4> global;
    if (!ResolveAllGlobals(bones, pose, rootTransform, global)) {
        return false;
    }
    out = global[static_cast<std::size_t>(boneIndex)];
    return true;
}

void Skeleton::ComputePalette(const SkeletonPose& pose, std::vector<Matrix4>& outPalette) const
{
    const std::size_t count = bones.size();
    outPalette.resize(count);
    if (count == 0 || pose.local.size() != count) {
        return;
    }

    std::vector<Matrix4> global;
    if (!ResolveAllGlobals(bones, pose, rootTransform, global)) {
        outPalette.clear();
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        outPalette[i] = Matrix4::Multiply(global[i], bones[i].inverseBind);
    }
}

} // namespace Concord::Animation
