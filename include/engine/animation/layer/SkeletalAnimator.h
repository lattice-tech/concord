#ifndef CONCORD_SKELETALANIMATOR_H
#define CONCORD_SKELETALANIMATOR_H

#include "Concord/CExport.h"
#include "engine/animation/layer/SkeletalLayer.h"
#include "engine/animation/state/SkeletalStateMachine.h"

#include <string>
#include <vector>

namespace Concord::Object {
class SkinnedModel;
}

namespace Concord::Animation {

/**
 * @brief Composes a character's whole skeletal animation: a base state
 * machine plus stacked layers and additive tracks, with root motion.
 *
 * The production entry point for rigged characters (one per character):
 * - The **base** is a SkeletalStateMachine (locomotion: states, blend
 *   spaces, parameter-driven crossfades). Access it through BaseMachine() and
 *   configure it exactly like a standalone machine.
 * - **Layers** (AddLayer) stack on top. A normal layer blends its masked
 *   bones toward the layer's pose (upper-body attack over running legs); an
 *   additive layer adds its pose-vs-bind difference (aim offsets, breathing).
 *   Layers are evaluated in the order they were added, so later layers win
 *   where masks overlap.
 * - **Root motion** (SetRootBone) measures the composed pose's root-bone
 *   world motion frame to frame and applies it to the target node, resetting
 *   the bone to bind so the mesh is not displaced twice. Loop clips must
 *   return their root to the same pose at the wrap point for the delta to
 *   stay continuous.
 *
 * Everything is driven by one parameter store (Parameters()); the base machine
 * and every layer read from it. Update(dt) evaluates base + layers, applies
 * the composed pose to the target and delivers root motion to the node.
 */
class CENGINE_API SkeletalAnimator {
public:
    /** The skinned model this animator drives; supplies the skeleton. */
    void SetTarget(Object::SkinnedModel* model) noexcept
    {
        m_target = model;
        m_baseMachine.SetTarget(model);
        // A new target has no previous composed pose: the first root-motion
        // frame establishes the baseline instead of reporting a jump.
        m_hasPrevRoot = false;
    }

    /** The base locomotion state machine (configure like a standalone one). */
    SkeletalStateMachine& BaseMachine() noexcept { return m_baseMachine; }

    /**
     * @brief Adds a layer with @p name, evaluated after every existing layer.
     * @return The new layer, or nullptr when the name is already taken.
     */
    SkeletalLayer* AddLayer(const std::string& name);

    /** A layer by name, or nullptr. */
    SkeletalLayer* FindLayer(const std::string& name) noexcept;

    /** Removes a layer by name; false when it does not exist. */
    bool RemoveLayer(const std::string& name);

    /** Sets a layer's blend weight by name (clamped to [0, 1]); false when absent. */
    bool SetLayerWeight(const std::string& name, float weight);

    /**
     * @brief Enables root motion for @p boneIndex (-1 disables).
     *
     * The composed pose's root-bone world motion is applied to the target
     * node each frame and the bone reset to bind in the skinning pose.
     */
    void SetRootBone(int boneIndex) noexcept { m_rootBone = boneIndex; }

    /** The root bone consumed for root motion, or -1 when disabled. */
    int RootBone() const noexcept { return m_rootBone; }

    /** Shared parameter store for the base machine and every layer. */
    AnimationParameters& Parameters() noexcept { return m_params; }
    const AnimationParameters& Parameters() const noexcept { return m_params; }

    /**
     * @brief Evaluates base + layers and applies the result to the target.
     *
     * Base pose is the machine's blended pose; each normal layer blends its
     * masked bones toward the layer's pose by the layer weight, each additive
     * layer adds its pose-vs-bind difference. Root motion, when enabled, is
     * applied to the target node afterwards.
     */
    void Update(float deltaTime);

private:
    struct LayerEntry {
        std::string name;
        SkeletalLayer layer;
    };

    SkeletalLayer* FindLayerInternal(const std::string& name) noexcept;

    Object::SkinnedModel* m_target = nullptr;
    SkeletalStateMachine m_baseMachine;
    std::vector<LayerEntry> m_layers;
    AnimationParameters m_params;
    int m_rootBone = -1;

    // Scratch poses reused across frames to avoid per-frame allocation.
    SkeletonPose m_basePose;
    SkeletonPose m_layerPose;
    SkeletonPose m_composed;
    Transform m_prevRootPose;
    bool m_hasPrevRoot = false;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALANIMATOR_H
