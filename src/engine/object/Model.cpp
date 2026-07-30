#include "engine/object/Model.h"

#include "engine/asset/import/ModelGeometry.h"
#include "engine/asset/import/ModelLoader.h"
#include "engine/collision/AabbOps.h"
#include "engine/debug/Logger.h"
#include "engine/material/MaterialDesc.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/render/mesh/MeshBounds.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/scene/Scene.h"

#include <cstring>
#include <chrono>
#include <utility>

namespace Concord::Object {

namespace {

void FlipWinding(MeshData& data)
{
    auto flip = [](auto& indices) {
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            std::swap(indices[i + 1], indices[i + 2]);
        }
    };
    flip(data.indices);
    flip(data.indices32);
}

/**
 * Copies the node world matrix into a RenderInstance.
 *
 * Geometry is already in stable model space (importer bake + optional
 * FinalizeModelGeometry). Draw-time placement is **only** this matrix —
 * never a residual normalize/view/pivot matrix. That invariant is what keeps
 * imported meshes fixed in the world while the camera moves.
 */
void FillInstanceWorld(RenderInstance& instance, const float* world) noexcept
{
    std::memcpy(instance.world, world, sizeof(instance.world));
}

/** Publishes a measured sub-mesh box so the cull path stops assuming a unit cube. */
void FillInstanceLocalBounds(RenderInstance& instance,
                             const Collision::Aabb& bounds) noexcept
{
    if (!Collision::IsValidAabb(bounds)) {
        return;
    }
    instance.hasLocalBounds = true;
    instance.localMin[0] = bounds.min.x;
    instance.localMin[1] = bounds.min.y;
    instance.localMin[2] = bounds.min.z;
    instance.localMax[0] = bounds.max.x;
    instance.localMax[1] = bounds.max.y;
    instance.localMax[2] = bounds.max.z;
}

/** Inverted box standing for "no usable positions in this sub-mesh". */
constexpr Collision::Aabb UnknownBounds() noexcept
{
    return Collision::Aabb{Vector3{1.0f, 1.0f, 1.0f}, Vector3{-1.0f, -1.0f, -1.0f}};
}

} // namespace

Model::Model(ModelDesc desc)
    : m_desc(std::move(desc))
    , m_overrideMaterial(m_desc.overrideMaterial)
{
    SetLocalTransform(m_desc.transform);
    OnStart([this] { PrewarmMeshes(); });
    if (m_desc.path.empty()) {
        Debug::Logger::Warn("Asset", "Model created with no path");
        return;
    }

    // --- Stage 1: format import (author space, mesh/node matrices baked) ---
    m_imported = Asset::ModelLoader::Import(m_desc.path);
    if (!m_imported.HasGeometry()) {
        Debug::Logger::Warn("Asset", "Model '%s' produced no geometry", m_desc.path.c_str());
        return;
    }

    // --- Stage 2: optional winding fix (export-dialect) -------------------
    if (m_desc.flipWinding) {
        for (Asset::ImportedSubMesh& sub : m_imported.meshes) {
            FlipWinding(sub.geometry);
        }
    }

    // --- Stage 3: finalize into stable model space ------------------------
    // Normalize rewrites vertices once (XZ-center, uniform fit, ground y=0).
    // Runtime never re-applies this; CollectRender only multiplies WorldMatrix.
    Asset::ModelGeometryOptions geomOptions;
    geomOptions.normalize = m_desc.autoNormalize;
    geomOptions.fitExtent = 2.0f;
    Asset::FinalizeModelGeometry(m_imported, geomOptions);

    std::size_t totalVerts = 0;
    std::size_t totalTris = 0;
    for (const Asset::ImportedSubMesh& sub : m_imported.meshes) {
        totalVerts += sub.geometry.positions.size();
        totalTris += sub.geometry.indices.size() / 3;
        totalTris += sub.geometry.indices32.size() / 3;
    }
    Debug::Logger::Info(
        "Asset",
        "Model '%s': %zu sub-mesh(es), %zu verts, %zu tris, model-space bounds "
        "(%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f) normalize=%s",
        m_desc.path.c_str(),
        m_imported.meshes.size(),
        totalVerts,
        totalTris,
        m_imported.boundsMin.x, m_imported.boundsMin.y, m_imported.boundsMin.z,
        m_imported.boundsMax.x, m_imported.boundsMax.y, m_imported.boundsMax.z,
        m_desc.autoNormalize ? "on" : "off");

    m_meshes.resize(m_imported.meshes.size());
    m_meshFutures.resize(m_imported.meshes.size());

    // Measured after finalization, so the boxes match the vertices actually
    // uploaded; the runtime never rewrites geometry again (see stage 3).
    m_subMeshBounds.assign(m_imported.meshes.size(), UnknownBounds());
    for (std::size_t i = 0; i < m_imported.meshes.size(); ++i) {
        Collision::Aabb bounds{};
        if (ComputeMeshBounds(m_imported.meshes[i].geometry, bounds)) {
            m_subMeshBounds[i] = bounds;
        }
    }
}

void Model::PrewarmMeshes()
{
    for (std::size_t i = 0; i < m_imported.meshes.size(); ++i) {
        EnsureMesh(i);
    }
}

Model::~Model()
{
    if (Scene* scene = OwningScene()) {
        for (MeshHandle handle : m_meshes) {
            if (handle.IsValid()) {
                scene->ReleaseMesh(handle);
            }
        }
    }
}

void Model::SetMaterialOverride(Material::MaterialDesc material)
{
    m_desc.materialOverride = std::move(material);
    m_desc.overrideMaterial = true;
    m_overrideMaterial = true;
}

void Model::ClearMaterialOverride()
{
    m_desc.materialOverride = Material::MaterialDesc{};
    m_desc.overrideMaterial = false;
    m_overrideMaterial = false;
}

MeshHandle Model::EnsureMesh(std::size_t i) const
{
    if (i >= m_meshes.size()) {
        return MeshHandle::Invalid();
    }
    if (m_meshes[i].IsValid()) {
        return m_meshes[i];
    }
    Scene* scene = OwningScene();
    if (scene == nullptr) {
        return MeshHandle::Invalid();
    }
    std::future<MeshHandle>& future = m_meshFutures[i];
    if (future.valid()) {
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_meshes[i] = future.get();
        }
        return m_meshes[i];
    }
    future = scene->AcquireMeshAsync(m_imported.meshes[i].geometry);
    return MeshHandle::Invalid();
}

void Model::CollectRender(std::vector<RenderInstance>& out) const
{
    if (!m_imported.HasGeometry()) {
        return;
    }

    // Single source of placement: the scene-graph world matrix. Geometry is
    // fixed in model space; camera motion never rewrites vertices or bakes
    // a second transform here.
    const float* world = WorldMatrix();
    const float reflectivity = Reflectivity();
    for (std::size_t i = 0; i < m_imported.meshes.size(); ++i) {
        const MeshHandle mesh = i < m_meshes.size() ? m_meshes[i] : MeshHandle::Invalid();
        if (!mesh.IsValid()) {
            continue;
        }
        RenderInstance instance;
        FillInstanceWorld(instance, world);
        if (i < m_subMeshBounds.size()) {
            FillInstanceLocalBounds(instance, m_subMeshBounds[i]);
        }
        instance.mesh = mesh;
        if (m_overrideMaterial) {
            instance.material = ResolveMaterial(m_desc.materialOverride);
        } else {
            instance.material = ResolveMaterial(m_imported.meshes[i].material);
        }
        instance.material.reflectivity *= reflectivity;
        // Imported meshes often mix winding (export dialects). Always draw
        // both sides so walls/floors do not vanish when the camera faces a
        // back-wound face — "the solid facing me is gone".
        instance.material.cull = CullMode::None;
        instance.rayTraced = UsesRealtimeReflection();
        instance.reflectionOwner = instance.rayTraced ? ReflectionOwnerKey() : 0;
        out.push_back(instance);
    }
}

} // namespace Concord::Object
