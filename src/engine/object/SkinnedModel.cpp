#include "engine/object/SkinnedModel.h"

#include "engine/asset/import/ModelLoader.h"
#include "engine/collision/AabbOps.h"
#include "engine/debug/Logger.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/scene/Scene.h"

#include <cmath>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace Concord::Object {

namespace {

/** Rotates @p v by @p q (q * v * q^-1). */
Vector3 RotateVector(const Quaternion& q, const Vector3& v) noexcept
{
    const Quaternion p{v.x, v.y, v.z, 0.0f};
    const Quaternion r = q * p * Quaternion{-q.x, -q.y, -q.z, q.w};
    return Vector3{r.x, r.y, r.z};
}

/** Wraps/clamps a playback time into a clip's timeline per its PlaybackMode. */
float WrapClipTime(float time, float duration, Animation::PlaybackMode mode) noexcept
{
    if (duration <= 0.0f) {
        return 0.0f;
    }
    switch (mode) {
        case Animation::PlaybackMode::Once:
            return time < 0.0f ? 0.0f : (time > duration ? duration : time);
        case Animation::PlaybackMode::Loop: {
            float t = std::fmod(time, duration);
            if (t < 0.0f) {
                t += duration;
            }
            return t;
        }
        case Animation::PlaybackMode::PingPong: {
            const float period = 2.0f * duration;
            float t = std::fmod(time, period);
            if (t < 0.0f) {
                t += period;
            }
            return t <= duration ? t : period - t;
        }
    }
    return time;
}

} // namespace

SkinnedModel::SkinnedModel(SkinnedModelDesc desc)
    : m_transform(desc.transform)
{
    SetLocalTransform(m_transform);
    m_overrideMaterial = desc.overrideMaterial;
    m_materialOverride = desc.materialOverride;

    if (!desc.path.empty()) {
        // File path: import on a background thread so startup does not block on
        // synchronous glTF disk IO and parsing.
        m_pendingImportPath = desc.path;
        m_importFuture = std::async(std::launch::async, [path = desc.path] {
            return Asset::ModelLoader::Import(path);
        });
    } else {
        // Procedural: a single hand-built skinned mesh.
        m_skeleton = std::move(desc.skeleton);
        if (!desc.mesh.positions.empty()) {
            Asset::ImportedSubMesh sub;
            sub.geometry = std::move(desc.mesh);
            sub.material = desc.material;
            m_subMeshes.push_back(std::move(sub));
        }
    }

    m_meshes.resize(m_subMeshes.size());
    m_meshFutures.resize(m_subMeshes.size());

    // Rest bounds are grouped by bone once here; every frame only re-transforms
    // those small boxes by the palette instead of rescanning vertices.
    m_subMeshBoneBounds.resize(m_subMeshes.size());
    m_subMeshHasUnweighted.assign(m_subMeshes.size(), false);
    m_poseBounds.assign(m_subMeshes.size(), Collision::Aabb{Vector3{1.0f, 1.0f, 1.0f},
                                                            Vector3{-1.0f, -1.0f, -1.0f}});
    for (std::size_t i = 0; i < m_subMeshes.size(); ++i) {
        bool hasUnweighted = false;
        ComputeSkinnedBoneBounds(m_subMeshes[i].geometry, m_subMeshBoneBounds[i],
                                 hasUnweighted);
        m_subMeshHasUnweighted[i] = hasUnweighted;
    }

    // Seed the bind pose + palette so the mesh is drawable before a clip plays.
    m_pose = m_skeleton.BindPose();
    m_skeleton.ComputePalette(m_pose, m_palette);
    RefreshPoseBounds();

    OnUpdate([this](float dt) { Advance(dt); });

    // Imported clips auto-play once the background import resolves.
}

void SkinnedModel::PrewarmMeshes()
{
    for (std::size_t i = 0; i < m_subMeshes.size(); ++i) {
        EnsureMesh(i);
    }
}

SkinnedModel::~SkinnedModel()
{
    if (Scene* scene = OwningScene()) {
        for (MeshHandle handle : m_meshes) {
            if (handle.IsValid()) {
                scene->ReleaseMesh(handle);
            }
        }
    }
}

void SkinnedModel::SetMaterialOverride(Material::MaterialDesc material)
{
    m_materialOverride = std::move(material);
    m_overrideMaterial = true;
}

void SkinnedModel::ClearMaterialOverride()
{
    m_materialOverride = Material::MaterialDesc{};
    m_overrideMaterial = false;
}

void SkinnedModel::PlayClipIndex(std::size_t index, Animation::PlaybackMode mode)
{
    if (index < m_ownedClips.size()) {
        PlayClip(&m_ownedClips[index], mode);
    }
}

void SkinnedModel::PlayClip(const Animation::SkeletalClip* clip, Animation::PlaybackMode mode)
{
    m_clip = clip;
    m_mode = mode;
    m_time = 0.0f;
    m_pingPongReversing = false;
    m_eventSampler.Reset();
}

void SkinnedModel::Stop()
{
    m_clip = nullptr;
    m_time = 0.0f;
    m_pingPongReversing = false;
    m_eventSampler.Reset();
    m_pose = m_skeleton.BindPose();
    m_skeleton.ComputePalette(m_pose, m_palette);
    RefreshPoseBounds();
}

const Animation::SkeletalClip* SkinnedModel::ClipAt(std::size_t index) const
{
    return index < m_ownedClips.size() ? &m_ownedClips[index] : nullptr;
}

const Animation::SkeletalClip* SkinnedModel::FindClip(const std::string& name) const
{
    for (const Animation::SkeletalClip& clip : m_ownedClips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

void SkinnedModel::ApplyPose(const Animation::SkeletonPose& pose)
{
    m_externallyDriven = true;
    m_pose = pose;
    m_skeleton.ComputePalette(m_pose, m_palette);
    RefreshPoseBounds();
}

void SkinnedModel::RefreshPoseBounds()
{
    for (std::size_t i = 0; i < m_subMeshBoneBounds.size() && i < m_poseBounds.size(); ++i) {
        Collision::Aabb bounds{};
        if (ComputeSkinnedPoseBounds(m_subMeshBoneBounds[i], m_palette.data(),
                                     m_palette.size(), m_subMeshHasUnweighted[i],
                                     bounds)) {
            m_poseBounds[i] = bounds;
        }
    }
}

void SkinnedModel::Advance(float deltaTime)
{
    if (m_skeleton.Empty() || m_externallyDriven) {
        return;
    }
    if (m_clip != nullptr) {
        const float duration = m_clip->Duration();
        const float oldTime = m_time;
        float bounceBoundary = -1.0f;
        if (m_mode == Animation::PlaybackMode::PingPong && duration > 0.0f) {
            // Track the bounce explicitly so the frame that turns around can
            // deliver the events crossed in both directions (mirrors
            // AnimationPlayer).
            const float step = deltaTime * m_speed;
            m_time += m_pingPongReversing ? -step : step;
            if (m_time >= duration) {
                bounceBoundary = duration;
                m_time = duration - (m_time - duration);
                m_pingPongReversing = true;
            } else if (m_time < 0.0f) {
                bounceBoundary = 0.0f;
                m_time = -m_time;
                m_pingPongReversing = false;
            }
        } else {
            m_time = WrapClipTime(m_time + deltaTime * m_speed, duration, m_mode);
        }
        m_clip->Sample(m_time, m_skeleton, m_pose);
        if (m_rootMotionEnabled && m_clip->rootBone >= 0) {
            Vector3 positionDelta;
            Quaternion rotationDelta;
            if (bounceBoundary >= 0.0f) {
                // A PingPong bounce turns around mid-frame: deliver the root
                // motion of the outgoing segment, then the incoming segment
                // coming back. SampleRootMotion alone would misread the
                // reversed span as a loop wrap.
                const bool reachedEnd = bounceBoundary >= duration;
                Vector3 outgoingPosition;
                Quaternion outgoingRotation;
                Vector3 incomingForwardPosition;
                Quaternion incomingForwardRotation;
                if (m_clip->SampleRootMotion(oldTime, bounceBoundary, m_skeleton,
                                             outgoingPosition, outgoingRotation)
                    && m_clip->SampleRootMotion(m_time, bounceBoundary, m_skeleton,
                                                incomingForwardPosition,
                                                incomingForwardRotation)) {
                    // The incoming segment ran backwards: its delta is the
                    // negation of the forward span.
                    positionDelta = Vector3{
                        outgoingPosition.x - incomingForwardPosition.x,
                        outgoingPosition.y - incomingForwardPosition.y,
                        outgoingPosition.z - incomingForwardPosition.z,
                    };
                    rotationDelta = outgoingRotation
                        * Quaternion{-incomingForwardRotation.x,
                                     -incomingForwardRotation.y,
                                     -incomingForwardRotation.z,
                                     incomingForwardRotation.w};
                    ApplyRootMotion(positionDelta, rotationDelta);
                }
            } else if (m_clip->SampleRootMotion(oldTime, m_time, m_skeleton,
                                                positionDelta, rotationDelta)) {
                ApplyRootMotion(positionDelta, rotationDelta);
            }
            if (m_clip->rootBone < static_cast<int>(m_pose.local.size())) {
                // The root bone's motion is carried by the node now; reset it
                // to bind in the skinning pose so the mesh is not displaced
                // twice.
                m_pose.local[static_cast<std::size_t>(m_clip->rootBone)] =
                    m_skeleton.bones[static_cast<std::size_t>(m_clip->rootBone)].bindLocal;
            }
        }
        FireClipEvents(oldTime, m_time, bounceBoundary, duration);
    } else {
        m_pose = m_skeleton.BindPose();
    }
    m_skeleton.ComputePalette(m_pose, m_palette);
    RefreshPoseBounds();
    PrewarmMeshes();
}

void SkinnedModel::FireClipEvents(float oldTime, float newTime,
                                  float bounceBoundary, float duration)
{
    if (m_clip == nullptr || m_clip->events.Empty()) {
        return;
    }
    if (bounceBoundary >= 0.0f) {
        // A PingPong bounce crosses the boundary twice in one frame: once in
        // the outgoing direction, then again coming back. Deliver both
        // windows; the second starts fresh from the boundary.
        const bool reachedEnd = bounceBoundary >= duration;
        m_eventSampler.Collect(m_clip->events, bounceBoundary, duration,
                               m_mode, reachedEnd);
        m_eventSampler.SetTime(bounceBoundary);
        m_eventSampler.Collect(m_clip->events, newTime, duration, m_mode,
                               !reachedEnd);
        return;
    }
    const bool forward = newTime >= oldTime;
    m_eventSampler.Collect(m_clip->events, newTime, duration, m_mode, forward);
}

void SkinnedModel::SetAnimationEventCallback(
    std::function<void(const Animation::SkeletalEvent&)> callback)
{
    m_eventSampler.ClearCallbacks();
    m_eventSampler.AddCallback(std::move(callback));
}

void SkinnedModel::ApplyRootMotion(const Vector3& positionDelta,
                                   const Quaternion& rotationDelta)
{
    // The node sits at the scene root (see SetRootMotionEnabled), so local
    // and world coincide. The position delta lives in the skeleton's model
    // space: rotate it by the node's own facing before translating.
    const Quaternion facing = LocalTransform().rotation;
    const Vector3 worldDelta = RotateVector(facing, positionDelta);
    Translate(worldDelta);
    Rotate(rotationDelta);
}

MeshHandle SkinnedModel::EnsureMesh(std::size_t i) const
{
    if (i >= m_meshes.size()) {
        return MeshHandle::Invalid();
    }
    if (m_meshes[i].IsValid()) {
        return m_meshes[i];
    }
    Scene* scene = OwningScene();
    if (scene == nullptr || m_subMeshes[i].geometry.positions.empty()) {
        return MeshHandle::Invalid();
    }
    std::future<MeshHandle>& future = m_meshFutures[i];
    if (future.valid()) {
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_meshes[i] = future.get();
        }
        return m_meshes[i];
    }
    future = scene->AcquireMeshAsync(m_subMeshes[i].geometry);
    return m_meshes[i];
}

void SkinnedModel::PollImportedModel() const
{
    if (m_importResolved || !m_importFuture.valid()) {
        return;
    }
    if (m_importFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    m_importResolved = true;
    if (!m_pendingImportPath.has_value()) {
        m_importFuture.get();
        return;
    }
    SkinnedModel* self = const_cast<SkinnedModel*>(this);
    Asset::ImportedModel imported = m_importFuture.get();
    const std::string path = *m_pendingImportPath;
    if (!imported.IsSkinned()) {
        Debug::Logger::Warn("Asset", "SkinnedModel '%s' has no skin (not a rigged model)",
                            path.c_str());
        m_pendingImportPath.reset();
        return;
    }

    self->m_skeleton = std::move(imported.skeleton);
    self->m_subMeshes = std::move(imported.meshes);
    self->m_ownedClips = std::move(imported.clips);
    Debug::Logger::Info("Asset",
                        "SkinnedModel '%s': %zu bones, %zu sub-mesh(es), %zu clip(s)",
                        path.c_str(), self->m_skeleton.Count(), self->m_subMeshes.size(),
                        self->m_ownedClips.size());

    self->m_meshes.assign(self->m_subMeshes.size(), MeshHandle::Invalid());
    self->m_meshFutures.clear();
    self->m_meshFutures.resize(self->m_subMeshes.size());
    self->m_subMeshBoneBounds.resize(self->m_subMeshes.size());
    self->m_subMeshHasUnweighted.assign(self->m_subMeshes.size(), false);
    self->m_poseBounds.assign(self->m_subMeshes.size(), Collision::Aabb{Vector3{1.0f, 1.0f, 1.0f},
                                                                        Vector3{-1.0f, -1.0f, -1.0f}});
    for (std::size_t i = 0; i < self->m_subMeshes.size(); ++i) {
        bool hasUnweighted = false;
        ComputeSkinnedBoneBounds(self->m_subMeshes[i].geometry, self->m_subMeshBoneBounds[i],
                                 hasUnweighted);
        self->m_subMeshHasUnweighted[i] = hasUnweighted;
    }

    self->m_pose = self->m_skeleton.BindPose();
    self->m_skeleton.ComputePalette(self->m_pose, self->m_palette);
    self->RefreshPoseBounds();
    self->PrewarmMeshes();
    if (!self->m_ownedClips.empty()) {
        self->PlayClip(&self->m_ownedClips.front(), Animation::PlaybackMode::Loop);
    }
    m_pendingImportPath.reset();
}

void SkinnedModel::CollectRender(std::vector<RenderInstance>& out) const
{
    PollImportedModel();
    if (m_palette.empty() || m_subMeshes.empty()) {
        return;
    }
    const float* world = WorldMatrix();
    const float reflectivity = Reflectivity();
    for (std::size_t i = 0; i < m_subMeshes.size(); ++i) {
        const MeshHandle mesh = i < m_meshes.size() ? m_meshes[i] : MeshHandle::Invalid();
        if (!mesh.IsValid()) {
            continue;
        }
        RenderInstance instance;
        std::memcpy(instance.world, world, sizeof(instance.world));
        if (i < m_poseBounds.size() && Collision::IsValidAabb(m_poseBounds[i])) {
            instance.hasLocalBounds = true;
            instance.localMin[0] = m_poseBounds[i].min.x;
            instance.localMin[1] = m_poseBounds[i].min.y;
            instance.localMin[2] = m_poseBounds[i].min.z;
            instance.localMax[0] = m_poseBounds[i].max.x;
            instance.localMax[1] = m_poseBounds[i].max.y;
            instance.localMax[2] = m_poseBounds[i].max.z;
        }
        instance.mesh = mesh;
        instance.material = ResolveMaterial(m_overrideMaterial ? m_materialOverride
                                                              : m_subMeshes[i].material);
        instance.material.reflectivity *= reflectivity;
        // Rigged exports mix winding; draw both sides so faces never vanish.
        instance.material.cull = CullMode::None;
        instance.rayTraced = UsesRealtimeReflection();
        instance.reflectionOwner = instance.rayTraced ? ReflectionOwnerKey() : 0;
        // Palette lives in this node, rebuilt each Advance (same frame, before
        // CollectRender); the pointer stays valid through this frame's submit.
        instance.bonePalette = reinterpret_cast<const float*>(m_palette.data());
        instance.boneCount = static_cast<std::uint32_t>(m_palette.size());
        out.push_back(instance);
    }
}

} // namespace Concord::Object
