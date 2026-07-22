#include "engine/render/volume/BgfxSmokeRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_smoke_composite.bin.h"
#include "engine/render/shaders/generated/fs_smoke_march.bin.h"
#include "engine/render/shaders/generated/fs_smoke_march_single.bin.h"
#include "engine/render/shaders/generated/vs_smoke.bin.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

const bgfx::EmbeddedShader kSmokeShaders[] = {
    {
        "vs_smoke",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_smoke)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smoke_march",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smoke_march)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smoke_march_single",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smoke_march_single)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smoke_composite",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smoke_composite)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

void UnpackColor(std::uint32_t packed, float out[4]) noexcept
{
    out[0] = static_cast<float>((packed >> 24) & 0xffu) / 255.0f;
    out[1] = static_cast<float>((packed >> 16) & 0xffu) / 255.0f;
    out[2] = static_cast<float>((packed >> 8) & 0xffu) / 255.0f;
    out[3] = static_cast<float>(packed & 0xffu) / 255.0f;
}

/** Resolution of the precomputed tileable 3D noise texture (cubed). */
constexpr int kNoiseSize = 64;

/** Small integer hash -> [0,1); used to seed the periodic value noise lattice. */
float LatticeHash(int x, int y, int z, int period, unsigned seed) noexcept
{
    const int px = ((x % period) + period) % period;
    const int py = ((y % period) + period) % period;
    const int pz = ((z % period) + period) % period;
    unsigned h = seed + 0x9e3779b9u;
    h ^= static_cast<unsigned>(px) * 0x85ebca6bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<unsigned>(py) * 0xc2b2ae35u;
    h = (h << 17) | (h >> 15);
    h ^= static_cast<unsigned>(pz) * 0x27d4eb2fu;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0xffffff);
}

float SmoothStepScalar(float t) noexcept { return t * t * (3.0f - 2.0f * t); }

/** Periodic (tileable) value noise sampled at `uv` in [0,1) with `period` cells. */
float PeriodicValueNoise(float u, float v, float w, int period, unsigned seed) noexcept
{
    const float fx = u * static_cast<float>(period);
    const float fy = v * static_cast<float>(period);
    const float fz = w * static_cast<float>(period);
    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const int iz = static_cast<int>(std::floor(fz));
    const float tx = SmoothStepScalar(fx - static_cast<float>(ix));
    const float ty = SmoothStepScalar(fy - static_cast<float>(iy));
    const float tz = SmoothStepScalar(fz - static_cast<float>(iz));
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float n000 = LatticeHash(ix, iy, iz, period, seed);
    const float n100 = LatticeHash(ix + 1, iy, iz, period, seed);
    const float n010 = LatticeHash(ix, iy + 1, iz, period, seed);
    const float n110 = LatticeHash(ix + 1, iy + 1, iz, period, seed);
    const float n001 = LatticeHash(ix, iy, iz + 1, period, seed);
    const float n101 = LatticeHash(ix + 1, iy, iz + 1, period, seed);
    const float n011 = LatticeHash(ix, iy + 1, iz + 1, period, seed);
    const float n111 = LatticeHash(ix + 1, iy + 1, iz + 1, period, seed);
    const float low = lerp(lerp(n000, n100, tx), lerp(n010, n110, tx), ty);
    const float high = lerp(lerp(n001, n101, tx), lerp(n011, n111, tx), ty);
    return lerp(low, high, tz);
}

/** Tileable FBM: octaves whose periods double and all divide kNoiseSize. */
float PeriodicFbm(float u, float v, float w, int basePeriod, unsigned seed) noexcept
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    int period = basePeriod;
    for (int octave = 0; octave < 3; ++octave) {
        sum += amplitude * PeriodicValueNoise(u, v, w, period, seed + static_cast<unsigned>(octave));
        period *= 2;
        amplitude *= 0.5f;
    }
    return sum / 0.875f; // normalize (0.5 + 0.25 + 0.125) to ~[0,1]
}

/**
 * Builds a tileable 3D FBM noise volume: R holds a broad-shape FBM, G a
 * higher-frequency detail FBM. Baked once on the CPU and uploaded so the march
 * samples it with hardware trilinear filtering instead of ALU noise.
 */
const bgfx::Memory* BuildNoiseVolume() noexcept
{
    const int n = kNoiseSize;
    const bgfx::Memory* mem = bgfx::alloc(static_cast<std::uint32_t>(n * n * n * 4));
    std::uint8_t* data = mem->data;
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(n);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(n);
                const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(n);
                const float base = PeriodicFbm(u, v, w, 4, 0u);
                const float detail = PeriodicFbm(u, v, w, 8, 101u);
                std::uint8_t* texel = data + (static_cast<std::size_t>((z * n + y) * n + x) * 4);
                texel[0] = static_cast<std::uint8_t>(std::clamp(base, 0.0f, 1.0f) * 255.0f);
                texel[1] = static_cast<std::uint8_t>(std::clamp(detail, 0.0f, 1.0f) * 255.0f);
                texel[2] = 0;
                texel[3] = 255;
            }
        }
    }
    return mem;
}

/** First directional light in the frame drives the smoke sun; matches clouds. */
const Concord::RenderLight* FindSun(
    const Concord::RenderLight* lights, std::uint32_t lightCount) noexcept
{
    if (lights == nullptr) {
        return nullptr;
    }
    const std::uint32_t count = std::min(lightCount, Concord::kMaxRenderLights);
    const Concord::RenderLight* directional = nullptr;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (lights[index].type == Concord::LightType::Directional) {
            if (directional == nullptr) {
                directional = &lights[index];
            }
            if (lights[index].sun) {
                return &lights[index];
            }
        }
    }
    return directional;
}

} // namespace

namespace Concord {

bool BgfxSmokeRenderer::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kSmokeShaders, type, "vs_smoke");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kSmokeShaders, type, "fs_smoke_march");
    const bgfx::ShaderHandle fsSingle = bgfx::createEmbeddedShader(kSmokeShaders, type, "fs_smoke_march_single");
    const bgfx::ShaderHandle compFs = bgfx::createEmbeddedShader(kSmokeShaders, type, "fs_smoke_composite");
    const auto destroyIfValid = [](bgfx::ShaderHandle handle) {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
        }
    };
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs) || !bgfx::isValid(fsSingle) || !bgfx::isValid(compFs)) {
        Debug::Logger::Error("Render", "volumetric smoke shader creation failed");
        destroyIfValid(vs);
        destroyIfValid(fs);
        destroyIfValid(fsSingle);
        destroyIfValid(compFs);
        DestroyResources();
        return false;
    }
    // vs is consumed by three programs; keep it alive until the last one.
    m_compositeProgram = bgfx::createProgram(vs, compFs, false);
    m_programSingle = bgfx::createProgram(vs, fsSingle, false);
    m_program = bgfx::createProgram(vs, fs, false);
    destroyIfValid(vs);
    destroyIfValid(fs);
    destroyIfValid(fsSingle);
    destroyIfValid(compFs);

    m_uInvViewProj = bgfx::createUniform("u_smInvViewProj", bgfx::UniformType::Mat4);
    m_uCamera = bgfx::createUniform("u_smCamera", bgfx::UniformType::Vec4);
    m_uParams = bgfx::createUniform("u_smParams", bgfx::UniformType::Vec4);
    m_uSunDir = bgfx::createUniform("u_smSunDir", bgfx::UniformType::Vec4);
    m_uBoxMin = bgfx::createUniform("u_smBoxMin", bgfx::UniformType::Vec4, kMaxRenderSmokeVolumes);
    m_uBoxMax = bgfx::createUniform("u_smBoxMax", bgfx::UniformType::Vec4, kMaxRenderSmokeVolumes);
    m_uColor = bgfx::createUniform("u_smColor", bgfx::UniformType::Vec4, kMaxRenderSmokeVolumes);
    m_uShape = bgfx::createUniform("u_smShape", bgfx::UniformType::Vec4, kMaxRenderSmokeVolumes);
    m_uWind = bgfx::createUniform("u_smWind", bgfx::UniformType::Vec4, kMaxRenderSmokeVolumes);
    m_sSceneDepth = bgfx::createUniform("s_smSceneDepth", bgfx::UniformType::Sampler);
    m_sNoise = bgfx::createUniform("s_smNoise", bgfx::UniformType::Sampler);
    m_sComposite = bgfx::createUniform("s_smComposite", bgfx::UniformType::Sampler);
    m_sLowDepth = bgfx::createUniform("s_smLowDepth", bgfx::UniformType::Sampler);
    m_sFullDepth = bgfx::createUniform("s_smFullDepth", bgfx::UniformType::Sampler);
    m_uUpsample = bgfx::createUniform("u_smUpsample", bgfx::UniformType::Vec4);

    // Precomputed tileable 3D FBM noise, sampled with hardware trilinear
    // filtering + REPEAT wrap (default sampler flags) instead of ALU noise.
    if (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_3D) {
        m_noiseTexture = bgfx::createTexture3D(
            kNoiseSize, kNoiseSize, kNoiseSize, false,
            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, BuildNoiseVolume());
    } else {
        Debug::Logger::Error("Render", "3D textures unsupported; volumetric smoke disabled");
    }

    m_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    static const float kTriangle[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f,
    };
    m_fullscreenVb = bgfx::createVertexBuffer(bgfx::copy(kTriangle, sizeof(kTriangle)), m_layout);

    m_ready = bgfx::isValid(m_program)
        && bgfx::isValid(m_programSingle)
        && bgfx::isValid(m_compositeProgram)
        && bgfx::isValid(m_sComposite)
        && bgfx::isValid(m_sLowDepth)
        && bgfx::isValid(m_sFullDepth)
        && bgfx::isValid(m_uUpsample)
        && bgfx::isValid(m_uInvViewProj)
        && bgfx::isValid(m_uCamera)
        && bgfx::isValid(m_uParams)
        && bgfx::isValid(m_uSunDir)
        && bgfx::isValid(m_uBoxMin)
        && bgfx::isValid(m_uBoxMax)
        && bgfx::isValid(m_uColor)
        && bgfx::isValid(m_uShape)
        && bgfx::isValid(m_uWind)
        && bgfx::isValid(m_sSceneDepth)
        && bgfx::isValid(m_sNoise)
        && bgfx::isValid(m_noiseTexture)
        && bgfx::isValid(m_fullscreenVb);
    if (!m_ready) {
        Debug::Logger::Error("Render", "volumetric smoke resource initialization failed");
        DestroyResources();
    }
    return m_ready;
}

void BgfxSmokeRenderer::DestroyResources()
{
    if (bgfx::isValid(m_noiseTexture)) {
        bgfx::destroy(m_noiseTexture);
        m_noiseTexture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* uniform : {
             &m_uInvViewProj, &m_uCamera, &m_uParams, &m_uSunDir,
             &m_uBoxMin, &m_uBoxMax, &m_uColor, &m_uShape, &m_uWind,
             &m_sSceneDepth, &m_sNoise, &m_sComposite,
             &m_sLowDepth, &m_sFullDepth, &m_uUpsample}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::ProgramHandle* program : {&m_program, &m_programSingle, &m_compositeProgram}) {
        if (bgfx::isValid(*program)) {
            bgfx::destroy(*program);
            *program = BGFX_INVALID_HANDLE;
        }
    }
    m_ready = false;
}

void BgfxSmokeRenderer::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void BgfxSmokeRenderer::Draw(const DrawParams& params)
{
    if (params.marchView == kInvalidRenderView || !bgfx::isValid(params.sceneDepth)
        || params.viewMatrix == nullptr || params.projectionMatrix == nullptr
        || params.eye == nullptr || params.volumes == nullptr || params.volumeCount == 0
        || !EnsureReady()) {
        return;
    }
    if (!params.composeOnly
        && (params.compositeView == kInvalidRenderView
            || !bgfx::isValid(params.lowResFb) || !bgfx::isValid(params.lowResColor)
            || !bgfx::isValid(params.lowResDepth) || !bgfx::isValid(params.sceneFb))) {
        return;
    }

    const std::uint32_t count = std::min(params.volumeCount, kMaxRenderSmokeVolumes);
    if (params.volumeCount > kMaxRenderSmokeVolumes) {
        Debug::Logger::Warn("Render", "smoke volumes (%u) exceed the %u-volume limit; extra dropped",
                            params.volumeCount, kMaxRenderSmokeVolumes);
    }

    float viewProjection[16];
    float inverseViewProjection[16];
    bx::mtxMul(viewProjection, params.viewMatrix, params.projectionMatrix);
    bx::mtxInverse(inverseViewProjection, viewProjection);

    float sunDirection[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    const RenderLight* sun = FindSun(params.lights, params.lightCount);
    if (sun != nullptr) {
        std::copy_n(sun->direction, 3, sunDirection);
    }

    const float camera[4] = {
        params.eye[0], params.eye[1], params.eye[2],
        bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f,
    };
    // z: march steps. Fixed for the first animated version; a later phase can
    // scale it with volume size or a quality setting. Callers (e.g. the
    // reflection cubemap) may request fewer steps to stay cheap.
    constexpr float kMarchSteps = 48.0f;
    const float steps = params.steps > 0.0f ? params.steps : kMarchSteps;
    const float smokeParams[4] = {static_cast<float>(count), 1.0f, steps, 0.0f};

    float boxMin[kMaxRenderSmokeVolumes][4]{};
    float boxMax[kMaxRenderSmokeVolumes][4]{};
    float color[kMaxRenderSmokeVolumes][4]{};
    float shape[kMaxRenderSmokeVolumes][4]{};
    float wind[kMaxRenderSmokeVolumes][4]{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const RenderSmokeVolume& volume = params.volumes[i];
        boxMin[i][0] = volume.boxMin[0];
        boxMin[i][1] = volume.boxMin[1];
        boxMin[i][2] = volume.boxMin[2];
        boxMin[i][3] = volume.density;
        boxMax[i][0] = volume.boxMax[0];
        boxMax[i][1] = volume.boxMax[1];
        boxMax[i][2] = volume.boxMax[2];
        boxMax[i][3] = volume.noiseScale;
        UnpackColor(volume.color, color[i]);
        color[i][3] = volume.coverage;
        shape[i][0] = volume.edgeSoftness;
        shape[i][1] = volume.detail;
        shape[i][2] = volume.anisotropy;
        shape[i][3] = volume.shape == SmokeShape::Ellipsoid ? 1.0f : 0.0f;
        wind[i][0] = volume.windOffset[0];
        wind[i][1] = volume.windOffset[1];
        wind[i][2] = volume.windOffset[2];
        wind[i][3] = volume.emissive;
    }

    const auto setMarchUniforms = [&] {
        bgfx::setUniform(m_uInvViewProj, inverseViewProjection);
        bgfx::setUniform(m_uCamera, camera);
        bgfx::setUniform(m_uParams, smokeParams);
        bgfx::setUniform(m_uSunDir, sunDirection);
        bgfx::setUniform(m_uBoxMin, boxMin, kMaxRenderSmokeVolumes);
        bgfx::setUniform(m_uBoxMax, boxMax, kMaxRenderSmokeVolumes);
        bgfx::setUniform(m_uColor, color, kMaxRenderSmokeVolumes);
        bgfx::setUniform(m_uShape, shape, kMaxRenderSmokeVolumes);
        bgfx::setUniform(m_uWind, wind, kMaxRenderSmokeVolumes);
        bgfx::setTexture(0, m_sSceneDepth, params.sceneDepth);
        bgfx::setTexture(1, m_sNoise, m_noiseTexture);
    };

    if (params.composeOnly) {
        // Reflection cubemap face: a single full-resolution march composited
        // straight onto the already-configured view, in submission order after
        // its geometry. Premultiplied "over" blend, no depth test. Uses the
        // single-output program (no MRT depth proxy: the cubemap face has only
        // one color attachment).
        setMarchUniforms();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::setVertexBuffer(0, m_fullscreenVb);
        bgfx::submit(params.marchView, m_programSingle);
        return;
    }

    // Pass A: march at low resolution into the offscreen target, cleared to
    // transparent black and written opaquely, so it holds the premultiplied
    // smoke contribution (mirrors the volumetric cloud pipeline).
    bgfx::setViewFrameBuffer(params.marchView, params.lowResFb);
    bgfx::setViewRect(params.marchView, 0, 0, static_cast<std::uint16_t>(params.lowWidth),
                      static_cast<std::uint16_t>(params.lowHeight));
    bgfx::setViewClear(params.marchView, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewMode(params.marchView, bgfx::ViewMode::Sequential);
    bgfx::touch(params.marchView);
    setMarchUniforms();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(params.marchView, m_program);

    // Pass B: depth-aware (bilateral) 4-tap upsample of the low-res march,
    // composited over the full-res scene color with premultiplied alpha. No clear.
    const float upsampleParams[4] = {
        // Low-res texel size in UV units (both full- and low-res share the same
        // [0,1] UV space, so this is simply 1/lowWidth, 1/lowHeight).
        1.0f / static_cast<float>(std::max(params.lowWidth, 1u)),
        1.0f / static_cast<float>(std::max(params.lowHeight, 1u)),
        // Depth-similarity sharpness: higher values reject mismatched-depth taps
        // more aggressively (crisper silhouettes, more falls-back-to-nearest).
        800.0f,
        0.0f,
    };
    bgfx::setViewFrameBuffer(params.compositeView, params.sceneFb);
    bgfx::setViewRect(params.compositeView, 0, 0, static_cast<std::uint16_t>(params.fullWidth),
                      static_cast<std::uint16_t>(params.fullHeight));
    bgfx::setViewClear(params.compositeView, BGFX_CLEAR_NONE);
    bgfx::setViewMode(params.compositeView, bgfx::ViewMode::Sequential);
    bgfx::setUniform(m_uUpsample, upsampleParams);
    bgfx::setTexture(0, m_sComposite, params.lowResColor,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(1, m_sLowDepth, params.lowResDepth,
                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(2, m_sFullDepth, params.sceneDepth);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(params.compositeView, m_compositeProgram);
}

} // namespace Concord
