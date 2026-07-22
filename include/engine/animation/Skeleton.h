#ifndef CONCORD_SKELETON_H
#define CONCORD_SKELETON_H

#include "Concord/CExport.h"
#include "engine/object/Transform.h"
#include "math/Matrix4.h"

#include <string>
#include <vector>

namespace Concord::Animation {

/** A pose of a whole skeleton: one local-space transform per bone. */
struct SkeletonPose {
    /** Local transform of each bone, parallel to Skeleton::bones. */
    std::vector<Transform> local;
};

/**
 * One joint of a skeleton.
 *
 * `parent` indexes into the owning Skeleton's bone array (-1 for a root).
 * `inverseBind` maps a vertex from model space into this bone's local space at
 * bind time — the matrix the skinning palette multiplies each bone's animated
 * global transform by so the mesh deforms relative to its rest pose. `bindLocal`
 * is the default local transform used to seed a pose before animation overrides it.
 */
struct Bone {
    std::string name;
    int parent = -1;
    Matrix4 inverseBind{};
    Transform bindLocal{};
};

/**
 * A skinning skeleton: an ordered list of bones plus the machinery to turn an
 * animated SkeletonPose into the matrix palette a skinning shader consumes.
 *
 * Bones need not be topologically sorted; ComputePalette resolves parents
 * before children regardless of array order. The skeleton is pure data with no
 * GPU or file-format ties — importers (glTF) fill it, the SkinnedModel node
 * animates a pose over it and uploads the palette.
 */
class CENGINE_API Skeleton {
public:
    std::vector<Bone> bones;

    /**
     * Fixed pre-transform applied in front of every root bone's animated local
     * transform when computing the palette. Carries the world transform of the
     * skeleton root joint's non-joint ancestors (e.g. an armature node that
     * rotates the whole character upright / Z-up->Y-up), which the joint chain
     * itself does not include. Identity by default.
     */
    Matrix4 rootTransform{};

    bool Empty() const noexcept { return bones.empty(); }
    std::size_t Count() const noexcept { return bones.size(); }

    /** Index of the bone named @p name, or -1 if there is none. */
    int BoneIndex(const std::string& name) const;

    /** A fresh pose seeded from every bone's bind-time local transform. */
    SkeletonPose BindPose() const;

    /**
     * Computes the skinning matrix palette for @p pose into @p outPalette
     * (resized to bone count): for each bone, its animated global transform
     * times its inverse bind matrix. Parents are always resolved before
     * children. Column-major matrices, ready to upload as `u_bones[]`.
     */
    void ComputePalette(const SkeletonPose& pose, std::vector<Matrix4>& outPalette) const;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETON_H
