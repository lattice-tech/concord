#ifndef CONCORD_BGFXMESHSTORE_H
#define CONCORD_BGFXMESHSTORE_H

#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/resource/ResourcePool.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace Concord {

/**
 * Owns the engine's uploaded geometry: a sized `ResourcePool<BgfxMesh>` keyed
 * by `MeshHandle`, the shared position+normal+UV vertex layout, and the
 * staged-then-copied GPU buffer creation pipeline in `Create`.
 *
 * Each mesh keeps its local-space min/max AABB stashed CPU-side at upload,
 * so the directional-light shadow frustum can fit the scene from the pending
 * draw list without reading GPU vertex data back — `RenderAabbWorld` is given
 * the AABB as plain floats, and the per-frame world union is built entirely
 * on the host. Kept in its own unit (AGENTS.md §5) so the render backend's
 * scene-submission concern stays separate from mesh storage.
 *
 * All methods run on the render thread; only one mesh store per backend.
 */
class BgfxMeshStore {
public:
    /** GPU buffers behind one MeshHandle plus the cached local-space AABB. */
    struct BgfxMesh {
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
        std::uint32_t indexCount = 0;

        /** True when the buffer was uploaded with the skinned layout (bone idx/weight). */
        bool skinned = false;

        /** Local-space min/max of the uploaded positions, for shadow-frustum fitting. */
        float aabbMin[3]{0.0f, 0.0f, 0.0f};
        float aabbMax[3]{0.0f, 0.0f, 0.0f};

        /** Per-bone local-space bounds used to fit animated shadow casters. */
        std::vector<std::array<float, 6>> boneAabbs;
    };

    BgfxMeshStore();

    /** Builds the position+normal+UV vertex layout the backend uploads against. */
    void InitLayout();

    /**
     * Interleaves `data`'s positions/normals/UVs into a bgfx-owned vertex
     * buffer, sizes indices as 16- or 32-bit by what the data carries, and
     * returns a handle naming the buffers. On any partial failure the
     * already-created buffers are freed and an invalid handle is returned; an
     * empty/missing geometry list is reported with a diagnostic and rejected.
     */
    MeshHandle Create(const MeshData& data);

    /** Frees the GPU buffers named by `mesh`; a no-op for an invalid/stale handle. */
    void Destroy(MeshHandle mesh);

    /** Returns the mesh in the pool behind `mesh`, or nullptr if it is no longer live. */
    const BgfxMesh* Get(MeshHandle mesh) const noexcept { return m_meshes.Get(mesh); }

    /** The vertex layout every upload copies against; valid once `InitLayout` has run. */
    const bgfx::VertexLayout& Layout() const noexcept { return m_meshLayout; }

    /** Destroys every live mesh and resets the pool. Safe to call repeatedly. */
    void Clear();

private:
    /** Builds an interleaved static (pos/normal/uv) vertex buffer for `data`. */
    bgfx::VertexBufferHandle CreateStaticVertexBuffer(const MeshData& data) const;

    /** Builds an interleaved skinned (pos/normal/uv/indices/weight) vertex buffer for `data`. */
    bgfx::VertexBufferHandle CreateSkinnedVertexBuffer(const MeshData& data) const;

    bgfx::VertexLayout m_meshLayout;
    bgfx::VertexLayout m_skinnedLayout;
    ResourcePool<BgfxMesh, MeshTag> m_meshes;
};

} // namespace Concord

#endif // CONCORD_BGFXMESHSTORE_H
