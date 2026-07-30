#include "engine/render/water/WaterGridMesh.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <cstring>

namespace Concord {

bool WaterGridMesh::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    m_ready = true;
    return true;
}

const WaterGridMesh::Grid* WaterGridMesh::Acquire(std::uint32_t subdivisions)
{
    if (!EnsureReady()) {
        return nullptr;
    }
    const std::uint32_t count = std::clamp(subdivisions, 1u, kMaxSubdivisions);
    const auto cached = std::find_if(m_grids.begin(), m_grids.end(),
                                     [count](const Grid& grid) {
                                         return grid.subdivisions == count;
                                     });
    if (cached != m_grids.end()) {
        return &*cached;
    }

    const std::uint32_t side = count + 1u;
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(side) * side);
    for (std::uint32_t row = 0; row < side; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(count);
        for (std::uint32_t column = 0; column < side; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(count);
            // Unit grid centred on the origin; the vertex shader scales it to
            // the surface extent so one grid serves every body of this density.
            vertices.push_back(Vertex{u - 0.5f, 0.0f, v - 0.5f, u, v});
        }
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(count) * count * 6u);
    for (std::uint32_t row = 0; row < count; ++row) {
        for (std::uint32_t column = 0; column < count; ++column) {
            const std::uint32_t topLeft = row * side + column;
            const std::uint32_t topRight = topLeft + 1u;
            const std::uint32_t bottomLeft = topLeft + side;
            const std::uint32_t bottomRight = bottomLeft + 1u;
            // Counter-clockwise seen from above, matching the engine's winding
            // so back-face culling keeps the top of the water visible.
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    Grid grid;
    grid.subdivisions = count;
    grid.vertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(),
                   static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))),
        m_layout);
    grid.indices = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(),
                   static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t))),
        BGFX_BUFFER_INDEX32);
    if (!bgfx::isValid(grid.vertices) || !bgfx::isValid(grid.indices)) {
        Debug::Logger::Error("Render", "water grid %u could not be created", count);
        if (bgfx::isValid(grid.vertices)) { bgfx::destroy(grid.vertices); }
        if (bgfx::isValid(grid.indices)) { bgfx::destroy(grid.indices); }
        return nullptr;
    }
    Debug::Logger::Debug("Render", "water grid %u: %zu verts, %zu indices",
                         count, vertices.size(), indices.size());
    m_grids.push_back(grid);
    return &m_grids.back();
}

void WaterGridMesh::Shutdown()
{
    for (Grid& grid : m_grids) {
        if (bgfx::isValid(grid.vertices)) { bgfx::destroy(grid.vertices); }
        if (bgfx::isValid(grid.indices)) { bgfx::destroy(grid.indices); }
    }
    m_grids.clear();
    m_ready = false;
}

} // namespace Concord
