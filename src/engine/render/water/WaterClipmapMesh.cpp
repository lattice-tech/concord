#include "engine/render/water/WaterClipmapMesh.h"

#include "engine/debug/Logger.h"

#include <cmath>

namespace Concord {

bool WaterClipmapMesh::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    m_layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();

    const std::uint32_t side = kTileQuads + 1u;
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(side) * side);
    for (std::uint32_t row = 0; row < side; ++row) {
        for (std::uint32_t column = 0; column < side; ++column) {
            // Unit tile; the vertex shader scales and offsets it per tile, so one
            // buffer serves every level no matter how large.
            vertices.push_back(Vertex{static_cast<float>(column) / static_cast<float>(kTileQuads),
                                      static_cast<float>(row) / static_cast<float>(kTileQuads)});
        }
    }

    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(kTileQuads) * kTileQuads * 6u);
    for (std::uint32_t row = 0; row < kTileQuads; ++row) {
        for (std::uint32_t column = 0; column < kTileQuads; ++column) {
            const std::uint16_t topLeft = static_cast<std::uint16_t>(row * side + column);
            const std::uint16_t topRight = static_cast<std::uint16_t>(topLeft + 1u);
            const std::uint16_t bottomLeft = static_cast<std::uint16_t>(topLeft + side);
            const std::uint16_t bottomRight = static_cast<std::uint16_t>(bottomLeft + 1u);
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    m_vertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(),
                   static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))),
        m_layout);
    m_indices = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(),
                   static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))));
    m_ready = bgfx::isValid(m_vertices) && bgfx::isValid(m_indices);
    if (!m_ready) {
        Debug::Logger::Error("Render", "water clipmap tile buffers unavailable");
        Shutdown();
    }
    return m_ready;
}

void WaterClipmapMesh::Update(float viewerX, float viewerZ, float baseExtent,
                              std::uint32_t levels, std::uint32_t resolution)
{
    m_tiles.clear();
    if (!(baseExtent > 0.0f) || levels == 0 || resolution == 0) {
        return;
    }
    if (!std::isfinite(viewerX) || !std::isfinite(viewerZ)) {
        viewerX = 0.0f;
        viewerZ = 0.0f;
    }

    // One centre for every level, and the very same one the cascade bakes around
    // (WaterCascade::SnapCentre). Snapping each level to its own grid instead is
    // what tears the surface open: a level's middle 2x2 hole is cut on the
    // assumption that the finer level's block lands exactly there, and two centres
    // that rounded to different grids do not. The mismatch reaches half the
    // coarsest parity step and changes every time the viewer crosses any level's
    // grid line, so the gaps open, close and slide — thin moving slivers of
    // background between the rings. The shared step is an exact multiple of every
    // level's texel and vertex parity, so one snap keeps every ring nested and
    // none of them crawling.
    const float sharedCentreX = WaterCascade::SnapCentre(viewerX);
    const float sharedCentreZ = WaterCascade::SnapCentre(viewerZ);

    for (std::uint32_t level = 0; level < levels; ++level) {
        const float extent = baseExtent * std::pow(2.0f, static_cast<float>(level));
        // The block spans half its level's extent, so its outermost vertices sit a
        // quarter of the extent from the centre and always sample well inside the
        // level. Covering the full extent would put them on the texture border,
        // where the clamped lookup smears and follows the viewer instead of
        // staying put in the world.
        const float coverage = extent * 0.5f;
        const float tileSize = coverage / static_cast<float>(kTilesPerAxis);
        const float originX = sharedCentreX - coverage * 0.5f;
        const float originZ = sharedCentreZ - coverage * 0.5f;

        for (std::uint32_t row = 0; row < kTilesPerAxis; ++row) {
            for (std::uint32_t column = 0; column < kTilesPerAxis; ++column) {
                const bool middle = row >= 1 && row <= 2 && column >= 1 && column <= 2;
                // The level below already covers the middle 2x2 at twice the
                // density, so drawing it again would only cost fill rate and
                // produce z-fighting between two versions of the same surface.
                if (level > 0 && middle) {
                    continue;
                }
                Tile tile;
                tile.originX = originX + static_cast<float>(column) * tileSize;
                tile.originZ = originZ + static_cast<float>(row) * tileSize;
                tile.size = tileSize;
                tile.level = level;
                m_tiles.push_back(tile);
            }
        }
    }
}

void WaterClipmapMesh::Shutdown()
{
    if (bgfx::isValid(m_vertices)) {
        bgfx::destroy(m_vertices);
        m_vertices = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_indices)) {
        bgfx::destroy(m_indices);
        m_indices = BGFX_INVALID_HANDLE;
    }
    m_tiles.clear();
    m_ready = false;
}

} // namespace Concord
