#include "engine/object/SkinnedModel.h"

#include "engine/asset/import/ModelLoader.h"
#include "engine/debug/Logger.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/scene/Scene.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace Concord::Object {

namespace {

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
        // File path: import skeleton + skinned sub-meshes + animation clips.
        Asset::ImportedModel imported = Asset::ModelLoader::Import(desc.path);
        if (!imported.IsSkinned()) {
            Debug::Logger::Warn("Asset", "SkinnedModel '%s' has no skin (not a rigged model)",
                                desc.path.c_str());
        } else {
            m_skeleton = std::move(imported.skeleton);
            m_subMeshes = std::move(imported.meshes);
            m_ownedClips = std::move(imported.clips);
            Debug::Logger::Info("Asset",
                                "SkinnedModel '%s': %zu bones, %zu sub-mesh(es), %zu clip(s)",
                                desc.path.c_str(), m_skeleton.Count(), m_subMeshes.size(),
                                m_ownedClips.size());
        }
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

    // Seed the bind pose + palette so the mesh is drawable before a clip plays.
    m_pose = m_skeleton.BindPose();
    m_skeleton.ComputePalette(m_pose, m_palette);

    OnUpdate([this](float dt) { Advance(dt); });

    // Auto-play the first imported clip so a file-loaded character animates
    // immediately.
    if (!m_ownedClips.empty()) {
        PlayClip(&m_ownedClips.front(), Animation::PlaybackMode::Loop);
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
}

void SkinnedModel::Stop()
{
    m_clip = nullptr;
    m_time = 0.0f;
    m_pose = m_skeleton.BindPose();
    m_skeleton.ComputePalette(m_pose, m_palette);
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
}

void SkinnedModel::Advance(float deltaTime)
{
    if (m_skeleton.Empty() || m_externallyDriven) {
        return;
    }
    if (m_clip != nullptr) {
        const float duration = m_clip->Duration();
        m_time = WrapClipTime(m_time + deltaTime * m_speed, duration, m_mode);
        m_clip->Sample(m_time, m_skeleton, m_pose);
    } else {
        m_pose = m_skeleton.BindPose();
    }
    m_skeleton.ComputePalette(m_pose, m_palette);
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
    m_meshes[i] = scene->AcquireMesh(m_subMeshes[i].geometry);
    return m_meshes[i];
}

void SkinnedModel::CollectRender(std::vector<RenderInstance>& out) const
{
    if (m_palette.empty() || m_subMeshes.empty()) {
        return;
    }
    const float* world = WorldMatrix();
    const float reflectivity = Reflectivity();
    for (std::size_t i = 0; i < m_subMeshes.size(); ++i) {
        const MeshHandle mesh = EnsureMesh(i);
        if (!mesh.IsValid()) {
            continue;
        }
        RenderInstance instance;
        std::memcpy(instance.world, world, sizeof(instance.world));
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
