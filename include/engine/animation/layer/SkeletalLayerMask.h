#ifndef CONCORD_SKELETALLAYERMASK_H
#define CONCORD_SKELETALLAYERMASK_H

#include "engine/animation/skeleton/Skeleton.h"

#include <string>
#include <utility>
#include <vector>

namespace Concord::Animation {

/**
 * @brief Restricts a skeletal animation layer to a set of bones by name.
 *
 * An upper-body layer (attack/aim) masks in the arm and spine bones so the
 * layer's clip blends only those while the legs keep playing the base
 * locomotion. Bone names that do not exist in the target skeleton simply
 * never match — authoring for one rig is silently ignored on another.
 *
 * An empty mask affects **every** bone (the layer is full-body), which is the
 * convenient default for a layer that wants it all.
 */
class SkeletalLayerMask {
public:
    /** Adds one bone to the masked set (duplicates are ignored). */
    void Include(std::string boneName)
    {
        for (const std::string& bone : m_bones) {
            if (bone == boneName) {
                return;
            }
        }
        m_bones.push_back(std::move(boneName));
    }

    bool Empty() const noexcept { return m_bones.empty(); }

    /** True when this mask covers the bone at @p boneIndex of @p skeleton. */
    bool AffectsBone(const Skeleton& skeleton, int boneIndex) const
    {
        if (m_bones.empty()) {
            return true;
        }
        if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.bones.size())) {
            return false;
        }
        return AffectsNamed(skeleton.bones[static_cast<std::size_t>(boneIndex)].name);
    }

    /** True when the bone named @p boneName is covered by this mask. */
    bool AffectsNamed(const std::string& boneName) const
    {
        if (m_bones.empty()) {
            return true;
        }
        for (const std::string& bone : m_bones) {
            if (bone == boneName) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::string> m_bones;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALLAYERMASK_H
