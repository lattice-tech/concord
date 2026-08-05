#include "engine/animation/skeleton/SkeletonRemapper.h"

#include "engine/animation/clip/SkeletalClip.h"

namespace Concord::Animation {

bool SkeletonRemapper::Build(const Skeleton& source, const SkeletalClip& sourceClip,
                             const Skeleton& target)
{
    m_targetBonePerTrack.clear();
    m_targetBonePerTrack.reserve(sourceClip.tracks.size());
    for (const BoneTrack& track : sourceClip.tracks) {
        int targetBone = -1;
        if (track.boneIndex >= 0
            && track.boneIndex < static_cast<int>(source.bones.size())) {
            targetBone = target.BoneIndex(
                source.bones[static_cast<std::size_t>(track.boneIndex)].name);
        }
        m_targetBonePerTrack.push_back(targetBone);
    }
    return !m_targetBonePerTrack.empty();
}

std::size_t SkeletonRemapper::MappedTracks() const noexcept
{
    std::size_t count = 0;
    for (const int targetBone : m_targetBonePerTrack) {
        if (targetBone >= 0) {
            ++count;
        }
    }
    return count;
}

void SkeletonRemapper::Sample(const SkeletalClip& sourceClip, float time,
                              const Skeleton& target, SkeletonPose& out) const
{
    out = target.BindPose();
    if (m_targetBonePerTrack.size() != sourceClip.tracks.size()) {
        return;
    }
    for (std::size_t i = 0; i < sourceClip.tracks.size(); ++i) {
        const int targetBone = m_targetBonePerTrack[i];
        if (targetBone < 0 || targetBone >= static_cast<int>(out.local.size())) {
            continue;
        }
        const BoneTrack& track = sourceClip.tracks[i];
        Transform& local = out.local[static_cast<std::size_t>(targetBone)];
        if (!track.position.Empty()) {
            local.position = track.position.Sample(time);
        }
        if (!track.rotation.Empty()) {
            local.rotation = track.rotation.Sample(time);
        }
        if (!track.scale.Empty()) {
            local.scale = track.scale.Sample(time);
        }
    }
}

} // namespace Concord::Animation
