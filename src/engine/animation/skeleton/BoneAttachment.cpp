#include "engine/animation/skeleton/BoneAttachment.h"

#include "math/Affine.h"
#include "math/MatrixTransform.h"

#include <cmath>
#include <cstring>

namespace Concord::Animation {

bool BoneAttachment::Attach(Object::Node* node, const Skeleton* skeleton,
                            const std::string& boneName, const Transform& offset)
{
    if (node == nullptr || skeleton == nullptr) {
        return false;
    }
    const int boneIndex = skeleton->BoneIndex(boneName);
    if (boneIndex < 0) {
        return false;
    }
    m_node = node;
    m_skeleton = skeleton;
    m_boneIndex = boneIndex;
    m_offset = offset;
    return true;
}

void BoneAttachment::Detach() noexcept
{
    m_node = nullptr;
    m_skeleton = nullptr;
    m_boneIndex = -1;
    m_offset = {};
}

bool BoneAttachment::Update(const SkeletonPose& pose)
{
    if (!IsAttached()) {
        return false;
    }
    Matrix4 boneWorld;
    if (!m_skeleton->ComputeBoneWorld(pose, m_boneIndex, boneWorld)) {
        return false;
    }

    float parentWorld[16];
    if (Object::Node* parent = m_node->Parent()) {
        std::memcpy(parentWorld, parent->WorldMatrix(), sizeof(parentWorld));
    } else {
        std::memset(parentWorld, 0, sizeof(parentWorld));
        parentWorld[0] = 1.0f;
        parentWorld[5] = 1.0f;
        parentWorld[10] = 1.0f;
        parentWorld[15] = 1.0f;
    }
    float inverseParent[16];
    if (!AffineInvert(parentWorld, inverseParent)) {
        return false;
    }

    const Matrix4 offsetMtx = Matrix4::FromTransform(m_offset);
    Matrix4 target = Matrix4::Multiply(boneWorld, offsetMtx);
    Matrix4 local;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            local.m[col * 4 + row] =
                inverseParent[0 * 4 + row] * target.m[col * 4 + 0]
                + inverseParent[1 * 4 + row] * target.m[col * 4 + 1]
                + inverseParent[2 * 4 + row] * target.m[col * 4 + 2]
                + inverseParent[3 * 4 + row] * target.m[col * 4 + 3];
        }
    }

    Transform localTransform;
    if (!MatrixToTransform(local, localTransform)) {
        return false;
    }
    // Node rotations are applied by bx::mtxFromQuaternion, whose matrices are
    // the *inverse* of the standard quaternion rotation Matrix4 produces (a
    // yaw of +90 turns into -90 in the node's frame). Writing the conjugate
    // of the decomposed rotation cancels that so the node's world transform
    // lands exactly on the bone pose.
    localTransform.rotation.x = -localTransform.rotation.x;
    localTransform.rotation.y = -localTransform.rotation.y;
    localTransform.rotation.z = -localTransform.rotation.z;
    m_node->SetLocalTransform(localTransform);
    return true;
}

} // namespace Concord::Animation
