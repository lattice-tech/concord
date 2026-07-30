#ifndef CONCORD_WATERCLIPMAPMESH_H
#define CONCORD_WATERCLIPMAPMESH_H

#include "engine/render/water/WaterCascade.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace Concord {

/**
 * Concentric tile rings centred on the viewer, one ring per cascade level.
 *
 * A single fixed grid cannot serve a large water surface: sized for the horizon
 * it is too coarse underfoot (the surface reads as facets), and sized for
 * underfoot it wastes millions of vertices on the horizon. Crest's arrangement,
 * followed here, gives every level the same tile geometry but twice the world
 * size, so vertex density falls off with distance in step with the cascade's
 * texel density — each tile's vertices land roughly one per texel of the level
 * it belongs to, which is the density that neither aliases nor wastes work.
 *
 * Level 0 fills a 4x4 block of tiles; every level above it is the same 4x4 block
 * with the middle 2x2 removed, because the level below already covers that area.
 * All tiles share one vertex/index buffer and differ only in their per-draw
 * origin, size and level.
 *
 * Owned by the render thread; Shutdown must run before bgfx::shutdown.
 */
class WaterClipmapMesh {
public:
    /**
     * Quads per tile edge; 1089 vertices and 2048 triangles per tile.
     *
     * This is not a free quality dial: it has to keep vertex spacing at roughly
     * one cascade texel. A level's ring spans a quarter of its extent per tile
     * (see Update) against 256 texels across the whole extent, so 32 quads puts
     * about one vertex per texel. Coarser than that and the mesh cannot express
     * the waves the cascade holds, which is what makes the surface read as flat
     * facets; finer only burns vertices on detail the texture does not have.
     *
     * Must stay in sync with WATER_TILE_QUADS in vs_water, which needs the same
     * value to fold odd vertices onto the coarser grid.
     */
    static constexpr std::uint32_t kTileQuads = 32;

    /** Tiles per axis in a level's block. */
    static constexpr std::uint32_t kTilesPerAxis = 4;

    /** One tile placement for the vertex shader. */
    struct Tile {
        float originX = 0.0f;
        float originZ = 0.0f;
        float size = 0.0f;
        std::uint32_t level = 0;
    };

    bool EnsureReady();
    void Shutdown();

    /**
     * Rebuilds the tile placement around a viewer position.
     *
     * Tile origins snap to their level's texel grid for the same reason the
     * cascade centres do: a continuously moving grid makes the whole surface
     * appear to crawl even when the waves themselves are still.
     */
    void Update(float viewerX, float viewerZ, float baseExtent,
                std::uint32_t levels, std::uint32_t resolution);

    const std::vector<Tile>& Tiles() const noexcept { return m_tiles; }

    bgfx::VertexBufferHandle Vertices() const noexcept { return m_vertices; }
    bgfx::IndexBufferHandle Indices() const noexcept { return m_indices; }

private:
    struct Vertex {
        float x = 0.0f;
        float z = 0.0f;
    };

    bool m_ready = false;
    bgfx::VertexLayout m_layout;
    bgfx::VertexBufferHandle m_vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_indices = BGFX_INVALID_HANDLE;
    std::vector<Tile> m_tiles;
};

} // namespace Concord

#endif // CONCORD_WATERCLIPMAPMESH_H
