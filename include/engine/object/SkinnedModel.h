#ifndef CONCORD_SKINNEDMODEL_H
#define CONCORD_SKINNEDMODEL_H

#include "Concord/CExport.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/clip/SkeletalClip.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/animation/clip/SkeletalEventSampler.h"
#include "engine/asset/import/ImportedModel.h"
#include "engine/material/MaterialDesc.h"
#include "engine/object/Node.h"
#include "engine/collision/Aabb.h"
#include "engine/object/SkinnedModelDesc.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/render/mesh/SkinnedMeshBounds.h"
#include "math/Matrix4.h"

#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace Concord::Object {

/**
 * A renderable node whose geometry is deformed by an animated skeleton (GPU
 * linear-blend skinning).
 *
 * Two ways to build one (see SkinnedModelDesc):
 *   - From a rigged file (`desc.path`, a glTF/GLB with a skin): the skeleton,
 *     skinned sub-meshes and animation clips are imported and the first clip
 *     auto-plays.
 *   - Procedurally (`desc.mesh`/`skeleton`/`material`): a single hand-built
 *     skinned mesh; call PlayClip with your own SkeletalClip.
 *
 * Each frame it samples the playing clip into a pose, computes the bone matrix
 * palette, and emits one skinned RenderInstance per sub-mesh (all sharing the
 * palette). With no clip playing it holds the bind pose. Sub-meshes upload
 * lazily on first draw.
 */
class CENGINE_API SkinnedModel : public Node {
public:
    explicit SkinnedModel(SkinnedModelDesc desc = {});
    ~SkinnedModel() override;

    SkinnedModel(const SkinnedModel&) = delete;
    SkinnedModel& operator=(const SkinnedModel&) = delete;

    /** The skeleton driving this model. */
    const Animation::Skeleton& Skeleton() const noexcept { return m_skeleton; }

    /** Number of bones in the skeleton. */
    std::size_t BoneCount() const noexcept { return m_skeleton.Count(); }

    /** Number of animation clips imported from the file (0 for procedural models). */
    std::size_t ClipCount() const noexcept { return m_ownedClips.size(); }

    /** True when a file was parsed and produced a skinned mesh + skeleton. */
    bool IsValid() const noexcept { return !m_subMeshes.empty() && !m_skeleton.Empty(); }

    /**
     * Plays an imported clip by index (0-based) from the beginning. Out-of-range
     * indices are ignored. Convenience for file-loaded models.
     */
    void PlayClipIndex(std::size_t index, Animation::PlaybackMode mode = Animation::PlaybackMode::Loop);

    /** Starts playing @p clip from the beginning; nullptr stops and holds bind pose. */
    void PlayClip(const Animation::SkeletalClip* clip,
                  Animation::PlaybackMode mode = Animation::PlaybackMode::Loop);

    /** Stops playback; the mesh returns to its bind pose. */
    void Stop();

    /** An imported clip by index, or nullptr if out of range (for authoring a state machine). */
    const Animation::SkeletalClip* ClipAt(std::size_t index) const;

    /** An imported clip by name, or nullptr if none matches. */
    const Animation::SkeletalClip* FindClip(const std::string& name) const;

    /**
     * Applies an externally computed pose directly (used by SkeletalStateMachine,
     * which blends multiple clips): stores it and rebuilds the palette. Also
     * marks the model externally driven so its own clip playback stops fighting
     * the state machine for the pose.
     */
    void ApplyPose(const Animation::SkeletonPose& pose);

    /**
     * @brief Registers a callback for the playing clip's event markers.
     *
     * Markers defined on the clip's `events` track fire as this model's own
     * playback crosses them (the path driven by PlayClip, not ApplyPose).
     * Direction and loop wrapping are honoured; see SkeletalEventSampler.
     * Only one callback is stored; setting another replaces it.
     */
    void SetAnimationEventCallback(
        std::function<void(const Animation::SkeletalEvent&)> callback);

    /**
     * @brief Enables root motion for the playing clip's `rootBone`.
     *
     * The root bone's animated world motion is applied to this node's
     * transform each frame instead of deforming the mesh: the bone is reset
     * to its bind pose in the skinning pose (no double movement) and the
     * node translates/rotates by the per-frame delta. The node is assumed to
     * sit at the scene root (a character's usual place); a rotated parent
     * would skew the applied delta.
     */
    void SetRootMotionEnabled(bool enabled) noexcept { m_rootMotionEnabled = enabled; }

    /** True while root motion consumes the clip's root-bone motion. */
    bool IsRootMotionEnabled() const noexcept { return m_rootMotionEnabled; }

    /**
     * @brief Applies a model-space root-motion delta to this node.
     *
     * The position delta is rotated by the node's own facing before
     * translating; the rotation delta composes onto the node. Shared by the
     * built-in root-motion path and external animators (SkeletalAnimator).
     * Assumes the node sits at the scene root.
     */
    void ApplyRootMotion(const Vector3& positionDelta,
                         const Quaternion& rotationDelta);

    /** Playback rate multiplier (1 = real time). */
    void SetSpeed(float speed) noexcept { m_speed = speed; }

    /** Current playback time within the clip, in seconds. */
    float Time() const noexcept { return m_time; }

    /**
     * The pose currently driving the skin (clip playback or the last
     * ApplyPose). Read-only snapshot of the model's own state.
     */
    const Animation::SkeletonPose& CurrentPose() const noexcept
    {
        return m_pose;
    }

    /** Replaces the material applied to every imported or procedural sub-mesh. */
    void SetMaterialOverride(Material::MaterialDesc material);

    /** Clears the material override and restores each sub-mesh's source material. */
    void ClearMaterialOverride();

private:
    void Advance(float deltaTime);
    void FireClipEvents(float oldTime, float newTime, float bounceBoundary,
                        float duration);
    void PrewarmMeshes();
    void CollectRender(std::vector<RenderInstance>& out) const override;
    MeshHandle EnsureMesh(std::size_t i) const;
    void PollImportedModel() const;

    /**
     * Recomputes each sub-mesh's conservative model-space box for the current
     * palette. Called after every palette rebuild so extraction culling sees
     * the posed extents instead of assuming the unit cube.
     */
    void RefreshPoseBounds();

    Transform m_transform{};
    Animation::Skeleton m_skeleton;
    std::vector<Asset::ImportedSubMesh> m_subMeshes;
    bool m_overrideMaterial = false;
    Material::MaterialDesc m_materialOverride{};
    /** Clips owned by this model (imported from the file); referenced by m_clip. */
    std::vector<Animation::SkeletalClip> m_ownedClips;

    const Animation::SkeletalClip* m_clip = nullptr;
    Animation::PlaybackMode m_mode = Animation::PlaybackMode::Loop;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_pingPongReversing = false; ///< true while PingPong playback runs backwards
    /** True once a SkeletalStateMachine drives the pose via ApplyPose; disables own playback. */
    bool m_externallyDriven = false;
    /** Root motion consumes the clip's root-bone motion into this node. */
    bool m_rootMotionEnabled = false;
    Animation::SkeletalEventSampler m_eventSampler;

    Animation::SkeletonPose m_pose;
    /** Column-major bone matrices for the current pose; referenced by RenderInstance. */
    std::vector<Matrix4> m_palette;

    /** Rest bounds grouped by influencing bone, per sub-mesh; measured once on load. */
    std::vector<std::vector<SkinnedBoneBounds>> m_subMeshBoneBounds;
    /** True per sub-mesh when some vertex is unweighted and collapses to the origin. */
    std::vector<bool> m_subMeshHasUnweighted;
    /** Conservative model-space box per sub-mesh for the current palette. */
    std::vector<Collision::Aabb> m_poseBounds;

    mutable std::future<Asset::ImportedModel> m_importFuture;
    mutable bool m_importResolved = false;
    mutable std::optional<std::string> m_pendingImportPath;

    mutable std::vector<MeshHandle> m_meshes;
    mutable std::vector<std::future<MeshHandle>> m_meshFutures;
};

} // namespace Concord::Object

#endif // CONCORD_SKINNEDMODEL_H
