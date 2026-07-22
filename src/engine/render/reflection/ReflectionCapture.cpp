#include "engine/render/reflection/ReflectionCapture.h"

#include "engine/debug/Logger.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Concord {

bool ReflectionCapture::Targets::Valid() const noexcept
{
    if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        return false;
    }
    for (bgfx::FrameBufferHandle framebuffer : framebuffers) {
        if (!bgfx::isValid(framebuffer)) {
            return false;
        }
    }
    return true;
}

bool ReflectionCapture::EnsureTargets(Targets& targets) const
{
    if (targets.Valid()) {
        return true;
    }
    DestroyTargets(targets);

    constexpr std::uint16_t kSize = static_cast<std::uint16_t>(kResolution);
    const std::uint64_t colorFlags = BGFX_TEXTURE_RT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    const std::uint64_t depthFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY;
    if (!bgfx::isTextureValid(0, true, 1, bgfx::TextureFormat::RGBA16F, colorFlags)
        || !bgfx::isTextureValid(0, true, 1, bgfx::TextureFormat::D24S8, depthFlags)) {
        Debug::Logger::Error("Render", "reflection cubemap formats are unsupported");
        return false;
    }
    targets.color = bgfx::createTextureCube(
        kSize, false, 1, bgfx::TextureFormat::RGBA16F, colorFlags);
    targets.depth = bgfx::createTextureCube(
        kSize, false, 1, bgfx::TextureFormat::D24S8, depthFlags);
    if (!bgfx::isValid(targets.color) || !bgfx::isValid(targets.depth)) {
        Debug::Logger::Error("Render", "reflection cubemap texture creation failed at %ux%u",
                             kResolution, kResolution);
        DestroyTargets(targets);
        return false;
    }

    for (std::uint16_t face = 0; face < kFaceCount; ++face) {
        bgfx::Attachment attachments[2];
        attachments[0].init(targets.color, bgfx::Access::Write, face, 1, 0,
                            BGFX_RESOLVE_NONE);
        attachments[1].init(targets.depth, bgfx::Access::Write, face, 1, 0,
                            BGFX_RESOLVE_NONE);
        targets.framebuffers[face] = bgfx::createFrameBuffer(2, attachments, false);
        if (!bgfx::isValid(targets.framebuffers[face])) {
            Debug::Logger::Error("Render", "reflection cubemap face %u creation failed",
                                 static_cast<unsigned>(face));
            DestroyTargets(targets);
            return false;
        }
    }
    return true;
}

void ReflectionCapture::DestroyTargets(Targets& targets) const
{
    for (bgfx::FrameBufferHandle& framebuffer : targets.framebuffers) {
        if (bgfx::isValid(framebuffer)) {
            bgfx::destroy(framebuffer);
            framebuffer = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(targets.color)) {
        bgfx::destroy(targets.color);
        targets.color = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(targets.depth)) {
        bgfx::destroy(targets.depth);
        targets.depth = BGFX_INVALID_HANDLE;
    }
}

bool ReflectionCapture::BuildFaceCamera(std::uint32_t face, const float position[3],
                                        bool homogeneousDepth,
                                        float outView[16], float outProj[16])
{
    if (face >= kFaceCount || position == nullptr || outView == nullptr || outProj == nullptr) {
        return false;
    }

    static constexpr bx::Vec3 kDirections[kFaceCount] = {
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f},
    };
    static constexpr bx::Vec3 kUp[kFaceCount] = {
        {0.0f,  1.0f,  0.0f},
        {0.0f,  1.0f,  0.0f},
        {0.0f,  0.0f, -1.0f},
        {0.0f,  0.0f,  1.0f},
        {0.0f,  1.0f,  0.0f},
        {0.0f,  1.0f,  0.0f},
    };

    const bx::Vec3 eye{position[0], position[1], position[2]};
    const bx::Vec3 at = bx::add(eye, kDirections[face]);
    bx::mtxLookAt(outView, eye, at, kUp[face], bx::Handedness::Left);
    bx::mtxProj(outProj, 90.0f, 1.0f, 0.05f, 100.0f, homogeneousDepth,
                bx::Handedness::Left);
    return true;
}

} // namespace Concord
