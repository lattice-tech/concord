#include "engine/render/reflection/PlanarReflection.h"

#include "engine/debug/Logger.h"

#include <bgfx/bgfx.h>
#include <algorithm>

namespace Concord {

void PlanarReflection::Shutdown()
{
}

bool PlanarReflection::EnsureTargets(std::uint32_t sceneWidth, std::uint32_t sceneHeight,
                                     Targets& out) const
{
    const std::uint32_t w = std::max(1u, static_cast<std::uint32_t>(
        static_cast<float>(sceneWidth) * kResolutionScale));
    const std::uint32_t h = std::max(1u, static_cast<std::uint32_t>(
        static_cast<float>(sceneHeight) * kResolutionScale));
    if (out.Valid() && out.width == w && out.height == h) {
        return true;
    }
    DestroyTargets(out);

    const std::uint16_t tw = static_cast<std::uint16_t>(w);
    const std::uint16_t th = static_cast<std::uint16_t>(h);
    const std::uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    const std::uint64_t depthFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY;
    out.color = bgfx::createTexture2D(tw, th, false, 1, bgfx::TextureFormat::RGBA16F, colorFlags);
    out.depth = bgfx::createTexture2D(tw, th, false, 1, bgfx::TextureFormat::D24S8, depthFlags);
    if (!bgfx::isValid(out.color) || !bgfx::isValid(out.depth)) {
        DestroyTargets(out);
        Debug::Logger::Error("Render", "planar reflection RT create failed at %ux%u", w, h);
        return false;
    }
    bgfx::TextureHandle attachments[2] = {out.color, out.depth};
    out.framebuffer = bgfx::createFrameBuffer(2, attachments, false);
    if (!bgfx::isValid(out.framebuffer)) {
        DestroyTargets(out);
        return false;
    }
    out.width = w;
    out.height = h;
    return true;
}

void PlanarReflection::DestroyTargets(Targets& targets) const
{
    if (bgfx::isValid(targets.framebuffer)) {
        bgfx::destroy(targets.framebuffer);
        targets.framebuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(targets.color)) {
        bgfx::destroy(targets.color);
        targets.color = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(targets.depth)) {
        bgfx::destroy(targets.depth);
        targets.depth = BGFX_INVALID_HANDLE;
    }
    targets.width = 0;
    targets.height = 0;
}

} // namespace Concord
