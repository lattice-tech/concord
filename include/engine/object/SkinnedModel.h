#ifndef CONCORD_SKINNEDMODEL_H
#define CONCORD_SKINNEDMODEL_H

#include "Concord/CExport.h"
#include "engine/animation/PlaybackMode.h"
#include "engine/animation/SkeletalClip.h"
#include "engine/animation/Skeleton.h"
#include "engine/asset/import/ImportedModel.h"
#include "engine/material/MaterialDesc.h"
#include "engine/object/Node.h"
#include "engine/object/SkinnedModelDesc.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"
#include "math/Matrix4.h"

#include <cstddef>
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

    /** Playback rate multiplier (1 = real time). */
    void SetSpeed(float speed) noexcept { m_speed = speed; }

    /** Current playback time within the clip, in seconds. */
    float Time() const noexcept { return m_time; }

    /** Replaces the material applied to every imported or procedural sub-mesh. */
    void SetMaterialOverride(Material::MaterialDesc material);

    /** Clears the material override and restores each sub-mesh's source material. */
    void ClearMaterialOverride();

private:
    void Advance(float deltaTime);
    void CollectRender(std::vector<RenderInstance>& out) const override;
    MeshHandle EnsureMesh(std::size_t i) const;

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
    /** True once a SkeletalStateMachine drives the pose via ApplyPose; disables own playback. */
    bool m_externallyDriven = false;

    Animation::SkeletonPose m_pose;
    /** Column-major bone matrices for the current pose; referenced by RenderInstance. */
    std::vector<Matrix4> m_palette;

    mutable std::vector<MeshHandle> m_meshes;
};

} // namespace Concord::Object

#endif // CONCORD_SKINNEDMODEL_H
