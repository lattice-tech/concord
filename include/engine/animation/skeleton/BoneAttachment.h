#ifndef CONCORD_BONEATTACHMENT_H
#define CONCORD_BONEATTACHMENT_H

#include "Concord/CExport.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/object/Node.h"
#include "math/Matrix4.h"

#include <string>

namespace Concord::Animation {

/**
 * @brief Ties a plain scene Node to one bone of an animated skeleton.
 *
 * The engine's standard way to put an object "in a character's hand": attach
 * a weapon, a muzzle flash, a follower camera. The node stays an ordinary
 * Node (own transform, scene hierarchy, own callbacks); this binder only
 * rewrites its local transform each frame so that, composed with its parent,
 * it lands at the bone's current world pose plus a small authored offset.
 *
 * Call Update once per frame with the latest SkeletonPose (the one the
 * SkinnedModel's palette was built from). The node's parent world matrix is
 * read live, so reparenting or moving the character keeps working.
 *
 * The binder holds non-owning pointers: node, skeleton and the pose passed
 * to Update must outlive the attachment.
 */
class CENGINE_API BoneAttachment {
public:
    BoneAttachment() = default;

    BoneAttachment(const BoneAttachment&) = delete;
    BoneAttachment& operator=(const BoneAttachment&) = delete;

    /**
     * @brief Binds @p node to @p boneName of @p skeleton.
     *
     * @p offset is a local-space transform relative to the bone (e.g. a
     * grip offset on a weapon). Replaces any previous binding.
     * @return false when the bone does not exist in the skeleton.
     */
    bool Attach(Object::Node* node, const Skeleton* skeleton,
                const std::string& boneName, const Transform& offset = {});

    /** Releases the binding; the node keeps its current transform. */
    void Detach() noexcept;

    /** True while a valid binding is installed. */
    bool IsAttached() const noexcept
    {
        return m_node != nullptr && m_skeleton != nullptr && m_boneIndex >= 0;
    }

    /** The bound node, or nullptr. */
    Object::Node* Node() const noexcept { return m_node; }

    /** The bound bone index, or -1. */
    int BoneIndex() const noexcept { return m_boneIndex; }

    /**
     * @brief Places the bound node at the bone's pose for @p pose.
     *
     * Computes the bone's world matrix, maps it into the node's parent space
     * (identity parent for a root node) and writes the resulting local
     * transform, including the authored offset.
     * @return false when not attached, the pose/skeleton mismatch, or a
     *         singular parent matrix makes the mapping impossible.
     */
    bool Update(const SkeletonPose& pose);

private:
    Object::Node* m_node = nullptr;
    const Skeleton* m_skeleton = nullptr;
    int m_boneIndex = -1;
    Transform m_offset{};
};

} // namespace Concord::Animation

#endif // CONCORD_BONEATTACHMENT_H
