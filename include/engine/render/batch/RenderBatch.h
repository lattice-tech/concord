#ifndef CONCORD_RENDERBATCH_H
#define CONCORD_RENDERBATCH_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/render/mesh/MeshHandle.h"

#include <cstddef>
#include <span>

namespace Concord {

/**
 * One instanced submit the render thread should issue this frame.
 *
 * Produces by RenderBatcher::Finish from the MeshDrawCommands that were
 * appended this frame: draws that share both a mesh buffer pair and a fully
 * equal resolved material collapse into one batch, so the backend issues a
 * single bgfx instanced submit for them instead of one submit per draw.
 *
 * `commands` is a span of pointers into the batcher's own flattened command
 * array; it is valid for as long as the batcher (or, equivalently, the
 * backend that owns it) holds them — typically one frame, consumed inside
 * RenderView before the next BeginFrame reorders them.
 */
struct RenderBatch {
    /** Mesh whose GPU vertex/index buffers every instance in this batch shares. */
    MeshHandle mesh{};

    /** Resolved material whose uniforms are set once for the whole batch. */
    RenderMaterial material{};

    /** Shader path shared by the batch. */
    RenderEffect effect = RenderEffect::Mesh;

    /** Whether the material samples the current real-time scene cubemap. */
    bool realtimeReflection = false;

    /**
     * Pointers to the original MeshDrawCommands that collapsed into this
     * batch, in submission order. Each entry's `worldMatrix` is one
     * instance's data uploaded to the instancing buffer.
     */
    std::span<const MeshDrawCommand* const> commands{};
};

} // namespace Concord

#endif // CONCORD_RENDERBATCH_H
