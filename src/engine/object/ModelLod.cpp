#include "engine/object/Model.h"

#include "engine/asset/import/ModelGeometry.h"
#include "engine/asset/import/ModelLoader.h"
#include "engine/debug/Logger.h"
#include "engine/scene/Scene.h"

#include <chrono>
#include <utility>

namespace Concord::Object {

bool Model::AddLodLevel(const std::string& path, float startDistance)
{
    if (!m_imported.HasGeometry()) {
        Debug::Logger::Warn("Asset", "AddLodLevel('%s') on a model with no geometry",
                            path.c_str());
        return false;
    }
    // Level 0 is the base model; the per-instance chain also carries it, so
    // only kMaxLodLevels - 1 coarser levels fit.
    if (m_lods.size() + 1 >= RenderInstance::kMaxLodLevels) {
        Debug::Logger::Warn("Asset", "AddLodLevel('%s'): level budget exhausted",
                            path.c_str());
        return false;
    }
    const float previous = m_lods.empty() ? 0.0f : m_lods.back().startDistance;
    if (!(startDistance > previous)) {
        Debug::Logger::Warn("Asset",
                            "AddLodLevel('%s'): start distance %.2f must exceed %.2f",
                            path.c_str(), startDistance, previous);
        return false;
    }

    LodLevel level;
    level.startDistance = startDistance;
    level.imported = Asset::ModelLoader::Import(path);
    if (!level.imported.HasGeometry()) {
        Debug::Logger::Warn("Asset", "LOD model '%s' produced no geometry", path.c_str());
        return false;
    }
    // Same finalize options as the base model so both levels occupy the same
    // model space and swap in place without popping position or scale.
    Asset::ModelGeometryOptions geomOptions;
    geomOptions.normalize = m_desc.autoNormalize;
    geomOptions.fitExtent = 2.0f;
    Asset::FinalizeModelGeometry(level.imported, geomOptions);

    level.meshes.resize(level.imported.meshes.size());
    level.futures.resize(level.imported.meshes.size());
    m_lods.push_back(std::move(level));
    return true;
}

MeshHandle Model::EnsureLodMesh(std::size_t level, std::size_t i) const
{
    if (level >= m_lods.size()) {
        return MeshHandle::Invalid();
    }
    const LodLevel& lod = m_lods[level];
    if (i >= lod.meshes.size()) {
        return MeshHandle::Invalid();
    }
    if (lod.meshes[i].IsValid()) {
        return lod.meshes[i];
    }
    Scene* scene = OwningScene();
    if (scene == nullptr) {
        return MeshHandle::Invalid();
    }
    std::future<MeshHandle>& future = lod.futures[i];
    if (future.valid()) {
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            lod.meshes[i] = future.get();
        }
        return lod.meshes[i];
    }
    future = scene->AcquireMeshAsync(lod.imported.meshes[i].geometry);
    return MeshHandle::Invalid();
}

void Model::FillInstanceLods(RenderInstance& instance, std::size_t i) const
{
    if (m_lods.empty()) {
        return;
    }
    instance.lodMeshes[0] = instance.mesh;
    instance.lodStartDistances[0] = 0.0f;
    std::uint32_t count = 1;
    for (std::size_t level = 0; level < m_lods.size()
             && count < RenderInstance::kMaxLodLevels; ++level) {
        // Sub-meshes pair by index; a coarser level with fewer sub-meshes
        // simply keeps the base mesh for the unpaired tail.
        if (i >= m_lods[level].imported.meshes.size()) {
            continue;
        }
        instance.lodMeshes[count] = EnsureLodMesh(level, i);
        instance.lodStartDistances[count] = m_lods[level].startDistance;
        ++count;
    }
    instance.lodCount = count > 1 ? count : 0;
}

} // namespace Concord::Object
