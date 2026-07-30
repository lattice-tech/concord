#include "engine/render/fluid/BgfxFluidRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/fluid/FluidMcTable.h"
#include "engine/render/fluid/FluidProgramSlots.h"
#include "engine/render/shaders/generated/cs_fluid_apply.bin.h"
#include "engine/render/shaders/generated/cs_fluid_density.bin.h"
#include "engine/render/shaders/generated/cs_fluid_divergence.bin.h"
#include "engine/render/shaders/generated/cs_fluid_field_splat.bin.h"
#include "engine/render/shaders/generated/cs_fluid_field_smooth.bin.h"
#include "engine/render/shaders/generated/cs_fluid_finalize.bin.h"
#include "engine/render/shaders/generated/cs_fluid_forces.bin.h"
#include "engine/render/shaders/generated/cs_fluid_grid_clear.bin.h"
#include "engine/render/shaders/generated/cs_fluid_grid_count.bin.h"
#include "engine/render/shaders/generated/cs_fluid_grid_scan.bin.h"
#include "engine/render/shaders/generated/cs_fluid_grid_scatter.bin.h"
#include "engine/render/shaders/generated/cs_fluid_integrate.bin.h"
#include "engine/render/shaders/generated/cs_fluid_mc_triangles.bin.h"
#include "engine/render/shaders/generated/cs_fluid_mc_voxels.bin.h"
#include "engine/render/shaders/generated/cs_fluid_pressure.bin.h"
#include "engine/render/shaders/generated/fs_fluid_surface.bin.h"
#include "engine/render/shaders/generated/vs_fluid_surface.bin.h"

#include <bgfx/embedded_shader.h>

namespace {

#define CONCORD_FLUID_SHADER(name) \
    { #name, { BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, name) \
               {bgfx::RendererType::Count, nullptr, 0} } }

const bgfx::EmbeddedShader kFluidShaders[] = {
    CONCORD_FLUID_SHADER(cs_fluid_grid_clear),
    CONCORD_FLUID_SHADER(cs_fluid_grid_count),
    CONCORD_FLUID_SHADER(cs_fluid_grid_scan),
    CONCORD_FLUID_SHADER(cs_fluid_grid_scatter),
    CONCORD_FLUID_SHADER(cs_fluid_density),
    CONCORD_FLUID_SHADER(cs_fluid_forces),
    CONCORD_FLUID_SHADER(cs_fluid_divergence),
    CONCORD_FLUID_SHADER(cs_fluid_pressure),
    CONCORD_FLUID_SHADER(cs_fluid_apply),
    CONCORD_FLUID_SHADER(cs_fluid_integrate),
    CONCORD_FLUID_SHADER(cs_fluid_finalize),
    CONCORD_FLUID_SHADER(cs_fluid_field_splat),
    CONCORD_FLUID_SHADER(cs_fluid_field_smooth),
    CONCORD_FLUID_SHADER(cs_fluid_mc_voxels),
    CONCORD_FLUID_SHADER(cs_fluid_mc_triangles),
    CONCORD_FLUID_SHADER(vs_fluid_surface),
    CONCORD_FLUID_SHADER(fs_fluid_surface),
    BGFX_EMBEDDED_SHADER_END()
};

bgfx::ProgramHandle CreateComputeProgram(const char* name)
{
    const bgfx::ShaderHandle shader = bgfx::createEmbeddedShader(
        kFluidShaders, bgfx::RendererType::Vulkan, name);
    if (!bgfx::isValid(shader)) {
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(shader, true);
}

} // namespace

namespace Concord {

std::size_t BgfxFluidRenderer::FluidKeyHash::operator()(const FluidKey& key) const noexcept
{
    const std::size_t a = std::hash<std::uint32_t>{}(key.ownerView);
    const std::size_t b = std::hash<std::uintptr_t>{}(key.fluidKey);
    return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
}

bool BgfxFluidRenderer::Supported() const
{
    return (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) != 0;
}

bool BgfxFluidRenderer::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    if (m_attempted) {
        return false;
    }
    m_attempted = true;
    if (!Supported()) {
        Debug::Logger::Warn("Render", "compute unsupported; DFSPH fluid disabled");
        return false;
    }
    // Creation order must match FluidProgramSlot.
    const char* shaderNames[kProgramCount] = {
        "cs_fluid_grid_clear", "cs_fluid_grid_count", "cs_fluid_grid_scan",
        "cs_fluid_grid_scatter", "cs_fluid_density", "cs_fluid_forces",
        "cs_fluid_divergence", "cs_fluid_pressure", "cs_fluid_apply",
        "cs_fluid_integrate", "cs_fluid_finalize", "cs_fluid_field_splat",
        "cs_fluid_field_smooth", "cs_fluid_mc_voxels", "cs_fluid_mc_triangles",
    };
    for (std::uint32_t i = 0; i < kProgramCount; ++i) {
        m_programs[i] = CreateComputeProgram(shaderNames[i]);
        if (!bgfx::isValid(m_programs[i])) {
            Debug::Logger::Error("Render", "fluid compute program %u failed", i);
            Shutdown();
            return false;
        }
    }
    bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(
        kFluidShaders, bgfx::RendererType::Vulkan, "vs_fluid_surface");
    bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(
        kFluidShaders, bgfx::RendererType::Vulkan, "fs_fluid_surface");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "fluid surface shaders failed");
        Shutdown();
        return false;
    }
    m_drawProgram = bgfx::createProgram(vs, fs, true);

    m_uParams = bgfx::createUniform("u_fluidParams", bgfx::UniformType::Vec4, 16);
    m_sFieldTex = bgfx::createUniform("s_fieldTex", bgfx::UniformType::Sampler);
    m_uEye = bgfx::createUniform("u_fluidEye", bgfx::UniformType::Vec4);
    m_uScreen = bgfx::createUniform("u_fluidScreen", bgfx::UniformType::Vec4);
    m_uSunDir = bgfx::createUniform("u_fluidSunDir", bgfx::UniformType::Vec4);
    m_uSunColor = bgfx::createUniform("u_fluidSunColor", bgfx::UniformType::Vec4);
    m_uSkyZenith = bgfx::createUniform("u_fluidSkyZenith", bgfx::UniformType::Vec4);
    m_uSkyHorizon = bgfx::createUniform("u_fluidSkyHorizon", bgfx::UniformType::Vec4);
    m_uOptics = bgfx::createUniform("u_fluidOptics", bgfx::UniformType::Vec4);
    m_uColor = bgfx::createUniform("u_fluidColor", bgfx::UniformType::Vec4);
    m_uField0 = bgfx::createUniform("u_fluidField0", bgfx::UniformType::Vec4);
    m_uField1 = bgfx::createUniform("u_fluidField1", bgfx::UniformType::Vec4);
    m_uInvModel = bgfx::createUniform("u_fluidInvModel", bgfx::UniformType::Mat4);
    m_uModel = bgfx::createUniform("u_fluidModel", bgfx::UniformType::Mat4);
    m_sSceneColor = bgfx::createUniform("s_sceneColor", bgfx::UniformType::Sampler);
    m_sSceneDepth = bgfx::createUniform("s_sceneDepth", bgfx::UniformType::Sampler);
    m_sField3d = bgfx::createUniform("s_field", bgfx::UniformType::Sampler);

    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 1, bgfx::AttribType::Float).end();
    const bgfx::Memory* mem = bgfx::copy(Fluid::kFluidMcTriTable,
                                         sizeof(Fluid::kFluidMcTriTable));
    m_triTable = bgfx::createVertexBuffer(mem, layout, BGFX_BUFFER_COMPUTE_READ);

    m_ready = bgfx::isValid(m_drawProgram) && bgfx::isValid(m_triTable);
    if (!m_ready) {
        Debug::Logger::Error("Render", "fluid renderer resource creation failed");
        Shutdown();
    }
    return m_ready;
}

FluidGpuState& BgfxFluidRenderer::EnsureState(const FluidKey& key,
                                              const RenderFluid& fluid)
{
    FluidGpuState& state = m_bodies[key];
    if (!FluidGpuStateMatches(state, fluid)) {
        if (!CreateFluidGpuState(state, fluid)) {
            Debug::Logger::Error("Render", "fluid GPU state allocation failed (particles=%u boundary=%u field=%ux%ux%u)",
                                 fluid.particleCount, fluid.boundaryCount,
                                 fluid.fieldDims[0], fluid.fieldDims[1], fluid.fieldDims[2]);
        }
        state.seeded = false;
        state.firstField = true;
    }
    state.lastSeenFrame = m_frameNumber;
    return state;
}

void BgfxFluidRenderer::DestroyView(RenderViewHandle ownerView)
{
    for (auto it = m_bodies.begin(); it != m_bodies.end();) {
        if (it->first.ownerView == ownerView) {
            DestroyFluidGpuState(it->second);
            it = m_bodies.erase(it);
            continue;
        }
        ++it;
    }
}

void BgfxFluidRenderer::EndFrame()
{
    for (auto it = m_bodies.begin(); it != m_bodies.end();) {
        if (it->second.lastSeenFrame + 2 < m_frameNumber) {
            DestroyFluidGpuState(it->second);
            it = m_bodies.erase(it);
            continue;
        }
        ++it;
    }
}

void BgfxFluidRenderer::Shutdown()
{
    for (bgfx::ProgramHandle& program : m_programs) {
        if (bgfx::isValid(program)) {
            bgfx::destroy(program);
        }
        program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_drawProgram)) {
        bgfx::destroy(m_drawProgram);
    }
    m_drawProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uniforms[] = {
        m_uParams, m_sFieldTex, m_uEye, m_uScreen, m_uSunDir, m_uSunColor,
        m_uSkyZenith, m_uSkyHorizon, m_uOptics, m_uColor, m_uField0, m_uField1,
        m_uInvModel, m_uModel, m_sSceneColor, m_sSceneDepth,
        m_sField3d,
    };
    for (bgfx::UniformHandle& uniform : uniforms) {
        if (bgfx::isValid(uniform)) {
            bgfx::destroy(uniform);
        }
        uniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_triTable)) {
        bgfx::destroy(m_triTable);
    }
    m_triTable = BGFX_INVALID_HANDLE;
    for (auto& [key, state] : m_bodies) {
        DestroyFluidGpuState(state);
    }
    m_bodies.clear();
    m_loggedDrawActive = false;
    m_ready = false;
    m_attempted = false;
}

} // namespace Concord
