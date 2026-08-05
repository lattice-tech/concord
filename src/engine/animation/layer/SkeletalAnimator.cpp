#include "engine/animation/layer/SkeletalAnimator.h"

#include "engine/object/SkinnedModel.h"
#include "math/MatrixTransform.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Animation {
namespace {

/** Conjugate (inverse for unit quaternions) — flips the imaginary part. */
Quaternion Conjugate(const Quaternion& q) noexcept
{
    return Quaternion{-q.x, -q.y, -q.z, q.w};
}

/** Bone-wise TRS blend, matching BlendSkeletonPose's per-bone math. */
Transform BlendBoneTransform(const Transform& a, const Transform& b, float t)
{
    Transform out;
    out.position = AnimInterpolate(a.position, b.position, t);
    out.rotation = AnimInterpolate(a.rotation, b.rotation, t);
    out.scale = AnimInterpolate(a.scale, b.scale, t);
    return out;
}

} // namespace

SkeletalLayer* SkeletalAnimator::AddLayer(const std::string& name)
{
    if (FindLayerInternal(name) != nullptr) {
        return nullptr;
    }
    LayerEntry entry;
    entry.name = name;
    m_layers.push_back(std::move(entry));
    return &m_layers.back().layer;
}

SkeletalLayer* SkeletalAnimator::FindLayer(const std::string& name) noexcept
{
    return FindLayerInternal(name);
}

bool SkeletalAnimator::RemoveLayer(const std::string& name)
{
    for (std::size_t i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].name == name) {
            m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool SkeletalAnimator::SetLayerWeight(const std::string& name, float weight)
{
    SkeletalLayer* layer = FindLayerInternal(name);
    if (layer == nullptr) {
        return false;
    }
    layer->SetWeight(weight);
    return true;
}

SkeletalLayer* SkeletalAnimator::FindLayerInternal(const std::string& name) noexcept
{
    for (LayerEntry& entry : m_layers) {
        if (entry.name == name) {
            return &entry.layer;
        }
    }
    return nullptr;
}

void SkeletalAnimator::Update(float deltaTime)
{
    if (m_target == nullptr) {
        return;
    }
    const Skeleton& skeleton = m_target->Skeleton();
    const std::size_t boneCount = skeleton.bones.size();
    if (boneCount == 0) {
        return;
    }

    // Base pose comes from the locomotion state machine.
    m_baseMachine.Sample(deltaTime, m_basePose);
    if (m_basePose.local.size() != boneCount) {
        m_basePose = skeleton.BindPose();
    }
    m_composed = m_basePose;

    // Layers stack in order; later layers win where masks overlap.
    for (LayerEntry& entry : m_layers) {
        SkeletalLayer& layer = entry.layer;
        const float weight = std::clamp(layer.Weight(), 0.0f, 1.0f);
        if (weight <= 0.0f) {
            continue;
        }
        layer.Sample(deltaTime, skeleton, m_params, m_layerPose);
        if (m_layerPose.local.size() != boneCount) {
            continue;
        }
        for (std::size_t i = 0; i < boneCount; ++i) {
            if (!layer.Mask().AffectsBone(skeleton, static_cast<int>(i))) {
                continue;
            }
            if (layer.Additive()) {
                // Add the layer's pose-vs-bind difference (position and
                // rotation; scale is left to the base).
                const Transform& bind = skeleton.bones[i].bindLocal;
                const Transform& layerPose = m_layerPose.local[i];
                Transform& composed = m_composed.local[i];
                composed.position = Vector3{
                    composed.position.x + (layerPose.position.x - bind.position.x) * weight,
                    composed.position.y + (layerPose.position.y - bind.position.y) * weight,
                    composed.position.z + (layerPose.position.z - bind.position.z) * weight,
                };
                const Quaternion difference = Conjugate(bind.rotation) * layerPose.rotation;
                const Quaternion weighted = AnimInterpolate(Quaternion{}, difference, weight);
                composed.rotation = composed.rotation * weighted;
            } else {
                m_composed.local[i] = BlendBoneTransform(m_composed.local[i],
                                                         m_layerPose.local[i],
                                                         weight);
            }
        }
    }

    // Root motion: measure the composed pose's root-bone world motion and
    // hand it to the node, then reset the bone to bind so the mesh is not
    // displaced twice. Loop clips must return the root to the same pose at
    // the wrap point for the delta to stay continuous.
    if (m_rootBone >= 0 && m_rootBone < static_cast<int>(boneCount)) {
        Matrix4 rootWorld;
        if (skeleton.ComputeBoneWorld(m_composed, m_rootBone, rootWorld)) {
            Transform rootNow;
            if (MatrixToTransform(rootWorld, rootNow)) {
                if (m_hasPrevRoot) {
                    const Vector3 positionDelta = Vector3{
                        rootNow.position.x - m_prevRootPose.position.x,
                        rootNow.position.y - m_prevRootPose.position.y,
                        rootNow.position.z - m_prevRootPose.position.z,
                    };
                    const Quaternion rotationDelta =
                        Conjugate(m_prevRootPose.rotation) * rootNow.rotation;
                    m_target->ApplyRootMotion(positionDelta, rotationDelta);
                }
                m_prevRootPose = rootNow;
                m_hasPrevRoot = true;
                m_composed.local[static_cast<std::size_t>(m_rootBone)] =
                    skeleton.bones[static_cast<std::size_t>(m_rootBone)].bindLocal;
            }
        }
    }

    m_target->ApplyPose(m_composed);
}

} // namespace Concord::Animation
