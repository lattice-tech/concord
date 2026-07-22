#ifndef CONCORD_RENDERINSTANCE_H
#define CONCORD_RENDERINSTANCE_H

#include "engine/object/PrimitiveShape.h"
#include "engine/render/frame/RenderEffect.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/render/mesh/MeshHandle.h"

#include <cstdint>

namespace Concord {

/**
 * One draw the render thread should issue this frame: a fully composed world
 * matrix (scene-graph hierarchy + the object's own size already baked in), a
 * resolved material and which mesh to use.
 *
 * Produced by renderable nodes (see Object::Node::CollectRender) and gathered
 * by the Scene into a per-window list each frame, then consumed by the render
 * backend. It carries no identity or lifetime — it is a transient description
 * of "draw this, here, this frame".
 *
 * A custom mesh (uploaded through EngineLoop::CreateMesh) takes precedence
 * over `shape`: when `mesh` is valid the loop draws it directly, otherwise it
 * falls back to the built-in primitive named by `shape`.
 */
struct RenderInstance {
    /** Column-major 4x4 world matrix, ready for the vertex shader. */
    float world[16]{};

    /** Surface appearance for this draw (color, lighting response, gradient). */
    RenderMaterial material{};

    /** Which built-in mesh to draw (used when `mesh` is invalid). */
    Object::PrimitiveShape shape = Object::PrimitiveShape::Cube;

    /** Shader path used for this instance. */
    RenderEffect effect = RenderEffect::Mesh;

    /**
     * A custom uploaded mesh; when valid, drawn instead of `shape`. Set by
     * Object::Model (and anything holding a MeshHandle from
     * EngineLoop::CreateMesh). Defaults to the invalid handle so existing
     * primitive-based objects are unaffected.
     */
    MeshHandle mesh{};

    /**
     * When true this object keeps its mesh/depth/normal rasterization and samples
     * the current real-time HDR scene cubemap. Mirrors
     * Object::Node::UsesRealtimeReflection(); defaults false so existing
     * objects keep the standard PBR path. The legacy field name is retained
     * inside the render contract to avoid breaking custom backends.
     */
    bool rayTraced = false;

    /**
     * Equality-only identity shared by all reflective sub-meshes from one node.
     * It lets capture code union their bounds and exclude the whole receiver,
     * rather than treating one imported sub-mesh as the complete object.
     */
    std::uintptr_t reflectionOwner = 0;

    /**
     * Skinning matrix palette: `boneCount` column-major 4x4 matrices. When
     * non-null this instance is drawn through the skinned mesh path (see
     * MeshDrawCommand::bonePalette). Set by Object::SkinnedModel, which keeps
     * the backing storage alive for the frame. Null for ordinary rigid draws.
     */
    const float* bonePalette = nullptr;
    std::uint32_t boneCount = 0;
};

} // namespace Concord

#endif // CONCORD_RENDERINSTANCE_H
