#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/debug/Logger.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>

namespace {

/**
 * Rejects surfaces the pass cannot draw meaningfully.
 *
 * A non-finite matrix or a degenerate extent would produce either nothing or a
 * NaN-filled grid that poisons the whole HDR target, so it is dropped at the
 * submission boundary rather than inside the draw loop.
 */
bool IsDrawableSurface(const Concord::RenderWaterSurface& surface) noexcept
{
    if (!(surface.width > 0.0f) || !(surface.length > 0.0f)) {
        return false;
    }
    for (float component : surface.world) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    return std::isfinite(surface.state.time) && std::isfinite(surface.depth);
}

} // namespace

namespace Concord {

void BgfxRenderBackend::SubmitWaterSurface(RenderViewHandle view,
                                           const RenderWaterSurface& surface)
{
    if (!m_initialized || view == kInvalidRenderView || !IsDrawableSurface(surface)) {
        return;
    }
    m_pendingWaterSurfaces[view].push_back(surface);
}

void BgfxRenderBackend::RenderWater(ViewSlot& slot, const float viewMatrix[16],
                                    const float projectionMatrix[16], const float eye[3],
                                    float nearPlane, float farPlane,
                                    const SkyEnvironment& environment,
                                    const RenderLight* lights, std::uint32_t lightCount,
                                    const RenderWaterSurface* surfaces,
                                    std::uint32_t surfaceCount)
{
    if (surfaces == nullptr || surfaceCount == 0
        || slot.waterView == kInvalidRenderView
        || !bgfx::isValid(slot.scene.framebuffer)) {
        return;
    }
    BgfxWaterRenderer::DrawParams params;
    params.view = slot.waterView;
    params.bakeView = slot.waterBakeView;
    params.sceneFb = slot.scene.framebuffer;
    params.width = slot.width;
    params.height = slot.height;
    params.viewMatrix = viewMatrix;
    params.projectionMatrix = projectionMatrix;
    params.eye = eye;
    params.lights = lights;
    params.lightCount = lightCount;
    params.sceneColor = slot.scene.color;
    params.sceneDepth = slot.scene.depth;
    params.sceneColorCopy = slot.sceneColorCopy;
    params.sceneDepthCopy = slot.sceneDepthCopy;
    params.nearPlane = nearPlane;
    params.farPlane = farPlane;
    if (slot.planarValid) {
        params.planarColor = slot.planar.color;
        params.planarViewProj = slot.planarViewProj;
        // Offscreen targets are top-left origin on the backends whose
        // presentation origin differs, matching the mesh path's flip rule.
        params.flipPlanarV = !bgfx::getCaps()->originBottomLeft;
    }
    m_water.Draw(params, environment, surfaces, surfaceCount);
}

} // namespace Concord
