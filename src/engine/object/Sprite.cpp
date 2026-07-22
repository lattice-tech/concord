#include "engine/object/Sprite.h"

#include "engine/material/MaterialDesc.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Concord::Object {

namespace {

/** Builds a flat XY quad addressing atlas cell (col,row) of a columns x rows grid. */
MeshData BuildFrameQuad(int frame, int columns, int rows, Vector2 size)
{
    const int c = frame % columns;
    const int r = frame / columns;
    const float u0 = static_cast<float>(c) / static_cast<float>(columns);
    const float u1 = static_cast<float>(c + 1) / static_cast<float>(columns);
    // Atlas row 0 is the top; texture V increases downward, so row r maps to
    // [r/rows, (r+1)/rows] with the top edge at the smaller V.
    const float v0 = static_cast<float>(r) / static_cast<float>(rows);
    const float v1 = static_cast<float>(r + 1) / static_cast<float>(rows);

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;

    MeshData mesh;
    // 4 corners in the local XY plane, facing +Z. Top-left uses V0 so the atlas
    // cell is not upside down.
    mesh.positions = {
        {-hx,  hy, 0.0f}, // top-left
        { hx,  hy, 0.0f}, // top-right
        {-hx, -hy, 0.0f}, // bottom-left
        { hx, -hy, 0.0f}, // bottom-right
    };
    mesh.normals = {
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
    };
    mesh.uvs = {
        {u0, v0}, {u1, v0}, {u0, v1}, {u1, v1},
    };
    mesh.indices = {0, 2, 1, 1, 2, 3};
    return mesh;
}

} // namespace

Sprite::Sprite(SpriteDesc desc)
    : m_desc(std::move(desc))
{
    SetLocalTransform(m_desc.transform);
    const int cols = std::max(1, m_desc.columns);
    const int rows = std::max(1, m_desc.rows);
    m_frameCount = m_desc.frameCount > 0 ? m_desc.frameCount : cols * rows;
    m_frameCount = std::max(1, m_frameCount);
    m_frameMeshes.resize(static_cast<std::size_t>(m_frameCount));
    OnUpdate([this](float dt) { Advance(dt); });
}

Sprite::~Sprite()
{
    if (Scene* scene = OwningScene()) {
        for (MeshHandle handle : m_frameMeshes) {
            if (handle.IsValid()) {
                scene->ReleaseMesh(handle);
            }
        }
    }
}

void Sprite::Advance(float deltaTime)
{
    m_time += deltaTime;
    const float frameF = m_time * m_desc.fps;
    int frame = static_cast<int>(frameF);
    if (m_desc.loop) {
        frame %= m_frameCount;
    } else if (frame >= m_frameCount) {
        frame = m_frameCount - 1;
    }
    m_frame = frame;
}

MeshHandle Sprite::EnsureFrameMesh(int frame) const
{
    if (frame < 0 || frame >= static_cast<int>(m_frameMeshes.size())) {
        return MeshHandle::Invalid();
    }
    if (m_frameMeshes[frame].IsValid()) {
        return m_frameMeshes[frame];
    }
    Scene* scene = OwningScene();
    if (scene == nullptr) {
        return MeshHandle::Invalid();
    }
    const MeshData quad = BuildFrameQuad(frame, std::max(1, m_desc.columns),
                                         std::max(1, m_desc.rows), m_desc.size);
    m_frameMeshes[frame] = scene->AcquireMesh(quad);
    return m_frameMeshes[frame];
}

void Sprite::CollectRender(std::vector<RenderInstance>& out) const
{
    if (m_desc.texture.empty()) {
        return;
    }
    const MeshHandle mesh = EnsureFrameMesh(m_frame);
    if (!mesh.IsValid()) {
        return;
    }

    Material::MaterialDesc mat;
    mat.model = m_desc.unlit ? Material::MaterialModel::Unlit : Material::MaterialModel::Lit;
    mat.surface.albedo = 0xFFFFFFFFu; // white: show the texel unmodulated
    mat.textures.albedo.path = m_desc.texture;
    mat.draw.cull = CullMode::None; // a flat quad is visible from both sides

    RenderInstance instance;
    std::memcpy(instance.world, WorldMatrix(), sizeof(instance.world));
    instance.mesh = mesh;
    instance.material = ResolveMaterial(mat);
    out.push_back(instance);
}

} // namespace Concord::Object
