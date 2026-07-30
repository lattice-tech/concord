#ifndef CONCORD_MODEL_H
#define CONCORD_MODEL_H

#include "Concord/CExport.h"
#include "engine/asset/import/ImportedModel.h"
#include "engine/collision/Aabb.h"
#include "engine/object/ModelDesc.h"
#include "engine/object/Node.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/mesh/MeshHandle.h"

#include <vector>
#include <future>

namespace Concord::Object {

/**
 * A renderable node backed by an imported model file (OBJ, glTF/GLB, STL,
 * PLY, 3DS, DAE, ...).
 *
 * Created through Scene::Spawn<Model>(desc). Load pipeline (CPU, once):
 *   1. Format importer → author vertices with mesh/node matrices baked in.
 *   2. Optional winding flip.
 *   3. FinalizeModelGeometry → stable model space (optional autoNormalize).
 *   4. Lazy GPU upload per sub-mesh on first CollectRender.
 *
 * Draw pipeline (every frame): CollectRender copies **only** the node world
 * matrix into each instance. Geometry is never rewritten, never multiplied by
 * the view matrix, and never re-normalized — so the mesh stays fixed in the
 * world when the camera moves. A `materialOverride` replaces every sub-mesh
 * material when set.
 */
class CENGINE_API Model : public Node {
public:
    explicit Model(ModelDesc desc = {});
    ~Model() override;

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    /** The path this model was loaded from. */
    const std::string& Path() const noexcept { return m_desc.path; }

    /** The full descriptor this model was built from (used by scene serialization). */
    const ModelDesc& Desc() const noexcept { return m_desc; }

    /** True when the file parsed and at least one sub-mesh has geometry. */
    bool IsValid() const noexcept { return m_imported.HasGeometry(); }

    /** The number of independently drawable sub-meshes the file produced. */
    std::size_t SubMeshCount() const noexcept { return m_imported.meshes.size(); }

    /** Replaces the material applied to every sub-mesh from now on. */
    void SetMaterialOverride(Material::MaterialDesc material);

    /** Clears any material override, reverting to the file's materials. */
    void ClearMaterialOverride();

    /** True when every imported sub-mesh is using the material override. */
    bool HasMaterialOverride() const noexcept { return m_overrideMaterial; }

private:
    void PrewarmMeshes();
    void CollectRender(std::vector<RenderInstance>& out) const override;

    /** Uploads sub-mesh `i` to the GPU on first use; returns its handle. */
    MeshHandle EnsureMesh(std::size_t i) const;

    ModelDesc m_desc;
    bool m_overrideMaterial = false;
    Asset::ImportedModel m_imported;

    /** One lazy-uploaded GPU mesh per imported sub-mesh; invalid until first use. */
    mutable std::vector<MeshHandle> m_meshes;
    /** Non-blocking GPU upload futures for sub-meshes not resident yet. */
    mutable std::vector<std::future<MeshHandle>> m_meshFutures;

    /**
     * Model-space AABB per sub-mesh, measured once after the geometry is
     * finalized. An entry left inverted (see Collision::IsValidAabb) means the
     * sub-mesh had no usable positions, and culling falls back to the unit cube.
     */
    std::vector<Collision::Aabb> m_subMeshBounds;
};

} // namespace Concord::Object

#endif // CONCORD_MODEL_H
