#include "engine/render/backend/BgfxMeshPipeline.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_mesh.bin.h"
#include "engine/render/shaders/generated/fs_particle_billboard.bin.h"
#include "engine/render/shaders/generated/vs_mesh.bin.h"
#include "engine/render/shaders/generated/vs_mesh_skinned.bin.h"
#include "engine/render/shaders/generated/vs_particle_billboard.bin.h"

#include <bgfx/embedded_shader.h>

namespace {

// SPIR-V only — Concord does not ship DXBC/DXIL mesh shaders.
const bgfx::EmbeddedShader kMeshShaders[] = {
    {
        "vs_mesh",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_mesh)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "vs_mesh_skinned",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_mesh_skinned)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_mesh",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_mesh)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "vs_particle_billboard",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_particle_billboard)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_particle_billboard",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_particle_billboard)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

namespace Concord {

bool BgfxMeshPipeline::EnsureReady()
{
    if (m_programReady || m_programAttempted) {
        return m_programReady;
    }
    m_programAttempted = true;

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kMeshShaders, rendererType, "vs_mesh");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kMeshShaders, rendererType, "fs_mesh");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render",
                             "mesh shader creation failed for %s (vs=%d fs=%d)",
                             bgfx::getRendererName(rendererType),
                             bgfx::isValid(vs),
                             bgfx::isValid(fs));
        if (bgfx::isValid(vs)) {
            bgfx::destroy(vs);
        }
        if (bgfx::isValid(fs)) {
            bgfx::destroy(fs);
        }
        return false;
    }

    m_program = bgfx::createProgram(vs, fs, true);
    m_programReady = bgfx::isValid(m_program);
    if (!m_programReady) {
        Debug::Logger::Error("Render", "mesh program init failed");
        Shutdown();
        return false;
    }
    return true;
}

bool BgfxMeshPipeline::EnsureSkinnedReady()
{
    if (m_skinnedReady || m_skinnedAttempted) {
        return m_skinnedReady;
    }
    m_skinnedAttempted = true;

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kMeshShaders, rendererType, "vs_mesh_skinned");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kMeshShaders, rendererType, "fs_mesh");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "skinned mesh shader creation failed (vs=%d fs=%d)",
                             bgfx::isValid(vs), bgfx::isValid(fs));
        if (bgfx::isValid(vs)) {
            bgfx::destroy(vs);
        }
        if (bgfx::isValid(fs)) {
            bgfx::destroy(fs);
        }
        return false;
    }
    m_skinnedProgram = bgfx::createProgram(vs, fs, true);
    m_skinnedReady = bgfx::isValid(m_skinnedProgram);
    if (!m_skinnedReady) {
        Debug::Logger::Error("Render", "skinned mesh program init failed");
    }
    return m_skinnedReady;
}

bool BgfxMeshPipeline::EnsureParticleReady()
{
    if (m_particleReady || m_particleAttempted) {
        return m_particleReady;
    }
    m_particleAttempted = true;

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(
        kMeshShaders, rendererType, "vs_particle_billboard");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(
        kMeshShaders, rendererType, "fs_particle_billboard");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "particle billboard shader creation failed (vs=%d fs=%d)",
                             bgfx::isValid(vs), bgfx::isValid(fs));
        if (bgfx::isValid(vs)) {
            bgfx::destroy(vs);
        }
        if (bgfx::isValid(fs)) {
            bgfx::destroy(fs);
        }
        return false;
    }
    m_particleProgram = bgfx::createProgram(vs, fs, true);
    m_particleReady = bgfx::isValid(m_particleProgram);
    if (!m_particleReady) {
        Debug::Logger::Error("Render", "particle billboard program init failed");
    }
    return m_particleReady;
}

void BgfxMeshPipeline::Shutdown()
{
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
    }
    m_program = BGFX_INVALID_HANDLE;
    m_programReady = false;
    if (bgfx::isValid(m_skinnedProgram)) {
        bgfx::destroy(m_skinnedProgram);
    }
    m_skinnedProgram = BGFX_INVALID_HANDLE;
    m_skinnedReady = false;
    if (bgfx::isValid(m_particleProgram)) {
        bgfx::destroy(m_particleProgram);
    }
    m_particleProgram = BGFX_INVALID_HANDLE;
    m_particleReady = false;
}

void BgfxMeshPipeline::Reset()
{
    Shutdown();
    m_programAttempted = false;
    m_skinnedAttempted = false;
    m_particleAttempted = false;
}

} // namespace Concord
