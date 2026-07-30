#include "engine/render/water/WaterCascade.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_water_bake.bin.h"
#include "engine/render/shaders/generated/vs_water_bake.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cmath>

namespace {

const bgfx::EmbeddedShader kBakeShaders[] = {
    {
        "vs_water_bake",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_water_bake)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_water_bake",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_water_bake)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

struct QuadVertex {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * Snaps a viewer coordinate to a multiple of @p step.
 *
 * The whole point of the cascade is that a texel maps to a fixed world size. If
 * the centre moved continuously, every texel would resample the wave field at a
 * slightly different offset each frame and the surface would visibly crawl.
 */
float SnapToStep(float value, float step) noexcept
{
    if (!(step > 0.0f) || !std::isfinite(value)) {
        return 0.0f;
    }
    return std::floor(value / step) * step;
}

} // namespace

namespace Concord {

float WaterCascade::SnapCentre(float value) noexcept
{
    return SnapToStep(value, kSharedSnap);
}

bool WaterCascade::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    if (m_attempted) {
        return false;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs =
        bgfx::createEmbeddedShader(kBakeShaders, type, "vs_water_bake");
    const bgfx::ShaderHandle fs =
        bgfx::createEmbeddedShader(kBakeShaders, type, "fs_water_bake");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "water cascade shader creation failed");
        if (bgfx::isValid(vs)) { bgfx::destroy(vs); }
        if (bgfx::isValid(fs)) { bgfx::destroy(fs); }
        return false;
    }
    m_bakeProgram = bgfx::createProgram(vs, fs, true);

    // One atlas, kLevels columns wide. Bilinear so the vertex stage can sample
    // between texels; clamped so a lookup at the edge of a level does not wrap
    // into its neighbour.
    m_atlas = bgfx::createTexture2D(
        static_cast<std::uint16_t>(kResolution * kLevels),
        static_cast<std::uint16_t>(kResolution), false, 1,
        bgfx::TextureFormat::RGBA16F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (bgfx::isValid(m_atlas)) {
        m_framebuffer = bgfx::createFrameBuffer(1, &m_atlas, false);
    }

    m_uLevel = bgfx::createUniform("u_waterCascadeLevel", bgfx::UniformType::Vec4);
    m_uSpectrum = bgfx::createUniform("u_waterCascadeSpectrum", bgfx::UniformType::Vec4);
    m_uGerstner = bgfx::createUniform("u_waterCascadeGerstner", bgfx::UniformType::Vec4);
    m_uWind = bgfx::createUniform("u_waterCascadeWind", bgfx::UniformType::Vec4);

    m_layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
    const QuadVertex quad[6] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
        {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    m_quadVb = bgfx::createVertexBuffer(bgfx::copy(quad, sizeof(quad)), m_layout);

    m_ready = bgfx::isValid(m_bakeProgram) && bgfx::isValid(m_atlas)
        && bgfx::isValid(m_framebuffer) && bgfx::isValid(m_uLevel)
        && bgfx::isValid(m_uSpectrum) && bgfx::isValid(m_uGerstner)
        && bgfx::isValid(m_uWind)
        && bgfx::isValid(m_quadVb);
    if (!m_ready) {
        Debug::Logger::Error("Render", "water cascade resources unavailable");
        DestroyResources();
    }
    return m_ready;
}

void WaterCascade::DestroyResources()
{
    if (bgfx::isValid(m_quadVb)) {
        bgfx::destroy(m_quadVb);
        m_quadVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* handle : {&m_uLevel, &m_uSpectrum, &m_uGerstner, &m_uWind}) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_framebuffer)) {
        bgfx::destroy(m_framebuffer);
        m_framebuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_atlas)) {
        bgfx::destroy(m_atlas);
        m_atlas = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_bakeProgram)) {
        bgfx::destroy(m_bakeProgram);
        m_bakeProgram = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

void WaterCascade::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void WaterCascade::Update(RenderViewHandle view, const RenderWaterSurface& surface,
                          float viewerX, float viewerZ)
{
    if (view == kInvalidRenderView || !EnsureReady()) {
        return;
    }
    if (!std::isfinite(viewerX) || !std::isfinite(viewerZ)) {
        viewerX = 0.0f;
        viewerZ = 0.0f;
    }

    bgfx::setViewFrameBuffer(view, m_framebuffer);
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(kResolution * kLevels),
                      static_cast<std::uint16_t>(kResolution));
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    // The bake overwrites every texel of every column, so no clear is needed.
    bgfx::touch(view);

    const bool useWind = surface.state.waveSource == Water::WaterWaveSource::WindSpectrum;
    const float choppiness = useWind
        ? std::clamp(surface.state.wind.choppiness, 0.0f, 1.7f)
        : (surface.waves.count > 0 ? surface.waves.waves[0].steepness : 0.0f);

    // One centre for every level, and the same one the clipmap uses. Snapping
    // each level to its own texel grid instead would put the levels at slightly
    // different centres, and since the mesh cuts each ring's hole where the next
    // level is *assumed* to start, any disagreement tears the rings open: a
    // moving crack up to a coarse tile wide. kSharedSnap is a whole multiple of
    // every level's texel size, so one centre still lands each level exactly on
    // its own texel grid and nothing crawls.
    const float centreX = SnapCentre(viewerX);
    const float centreZ = SnapCentre(viewerZ);

    for (std::uint32_t level = 0; level < kLevels; ++level) {
        const float extent = kBaseExtent * std::pow(2.0f, static_cast<float>(level));
        const float texel = extent / static_cast<float>(kResolution);
        m_levelParams[level][0] = centreX;
        m_levelParams[level][1] = centreZ;
        m_levelParams[level][2] = extent;
        m_levelParams[level][3] = texel;

        const float spectrum[4] = {surface.state.time, choppiness,
                                    static_cast<float>(level),
                                    static_cast<float>(kLevels)};
        float gerstner[4]{};
        float wind[4]{};
        if (useWind) {
            wind[0] = std::max(surface.state.wind.windSpeed, 0.0f);
            wind[1] = surface.state.wind.windDirectionDegrees;
            wind[2] = surface.state.wind.spreadDegrees;
            wind[3] = surface.state.wind.amplitudeScale;
        } else {
            const float amplitude = surface.waves.count > 0
                ? surface.waves.waves[0].amplitude : 0.0f;
            const float wavelength = surface.waves.count > 0
                ? std::max(surface.waves.waves[0].wavelength, 0.05f) : 8.0f;
            const float speed = surface.waves.count > 0
                ? surface.waves.waves[0].speed : 0.0f;
            float heading = 0.0f;
            if (surface.waves.count > 0) {
                heading = std::atan2(surface.waves.waves[0].directionZ,
                                     surface.waves.waves[0].directionX)
                    * 57.29577951f;
            }
            gerstner[0] = amplitude;
            gerstner[1] = wavelength;
            gerstner[2] = speed;
            gerstner[3] = heading;
        }
        bgfx::setUniform(m_uLevel, m_levelParams[level]);
        bgfx::setUniform(m_uSpectrum, spectrum);
        bgfx::setUniform(m_uGerstner, gerstner);
        bgfx::setUniform(m_uWind, wind);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::setVertexBuffer(0, m_quadVb);
        bgfx::submit(view, m_bakeProgram);
    }
}

} // namespace Concord
