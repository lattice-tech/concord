#ifndef CONCORD_WATERGRIDMESH_H
#define CONCORD_WATERGRIDMESH_H

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace Concord {

/**
 * Shared unit grids the water pass displaces into surfaces.
 *
 * The grid spans [-0.5, 0.5] on X and Z at y = 0 and is scaled to each body's
 * extent in the vertex shader, so every surface with the same subdivision count
 * shares one vertex/index buffer no matter how large it is. Grids are built on
 * first use and cached for the backend's lifetime: they are pure topology, so
 * rebuilding one per frame would be wasted upload bandwidth.
 *
 * Indices are 32-bit because a fine grid passes 65535 vertices quickly (a
 * 256-subdivision grid is 257^2 = 66049).
 *
 * Owned by the render thread; Shutdown must run before bgfx::shutdown.
 */
class WaterGridMesh {
public:
    /**
     * Upper bound on subdivisions per axis. 256 keeps the largest grid at about
     * 66k vertices / 1.3 MB, which is cheap next to the shading cost, while
     * still giving a metre of wave detail across a 256-unit lake.
     */
    static constexpr std::uint32_t kMaxSubdivisions = 256;

    /** One cached grid's buffers and its vertex count, for diagnostics. */
    struct Grid {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::uint32_t subdivisions = 0;
    };

    /** Creates the shared vertex layout; safe to call repeatedly. */
    bool EnsureReady();

    /**
     * Returns the grid for @p subdivisions (clamped to [1, kMaxSubdivisions]),
     * building it on first request. Returns nullptr when the buffers could not
     * be created, so the caller skips the draw instead of submitting garbage.
     */
    const Grid* Acquire(std::uint32_t subdivisions);

    /** Releases every cached grid and the layout. */
    void Shutdown();

    const bgfx::VertexLayout& Layout() const noexcept { return m_layout; }

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    bool m_ready = false;
    bgfx::VertexLayout m_layout;
    std::vector<Grid> m_grids;
};

} // namespace Concord

#endif // CONCORD_WATERGRIDMESH_H
