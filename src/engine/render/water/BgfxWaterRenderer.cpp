#include "engine/render/water/BgfxWaterRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/render/frame/ShadowCasterLight.h"
#include "engine/render/shaders/generated/fs_water.bin.h"
#include "engine/render/shaders/generated/vs_water.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <bx/math.h>

namespace {

const bgfx::EmbeddedShader kWaterShaders[] = {
    {
        "vs_water",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_water)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_water",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_water)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

float SrgbToLinear(float channel) noexcept
{
    return channel <= 0.04045f ? channel / 12.92f
                               : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

/** out = lhs * rhs (column-vector order); bx::mtxMul(out, a, b) yields b * a. */
void Multiply(float* out, const float* lhs, const float* rhs) noexcept
{
    bx::mtxMul(out, rhs, lhs);
}

/** Unpacks 0xRRGGBBAA into linear RGB, the space the shading works in. */
void UnpackLinear(std::uint32_t rgba, float out[3]) noexcept
{
    out[0] = SrgbToLinear(static_cast<float>((rgba >> 24) & 0xffu) / 255.0f);
    out[1] = SrgbToLinear(static_cast<float>((rgba >> 16) & 0xffu) / 255.0f);
    out[2] = SrgbToLinear(static_cast<float>((rgba >> 8) & 0xffu) / 255.0f);
}

/**
 * Whether a clipmap tile overlaps the authored body of water.
 *
 * The clipmap follows the viewer and so covers ground the body does not; drawing
 * those tiles would paint water over dry land. The test is a world-space AABB
 * around the body's rectangle, which is conservative for a rotated body — tiles
 * just outside a rotated edge still draw, and the vertex stage keeps them flat.
 */
bool IsTileInside(const Concord::RenderWaterSurface& surface,
                 const Concord::WaterClipmapMesh::Tile& tile) noexcept
{
    const float halfWidth = surface.width * 0.5f;
    const float halfLength = surface.length * 0.5f;
    // Extent of the rotated rectangle projected onto world X and Z.
    const float extentX = std::fabs(surface.world[0]) * halfWidth
        + std::fabs(surface.world[8]) * halfLength;
    const float extentZ = std::fabs(surface.world[2]) * halfWidth
        + std::fabs(surface.world[10]) * halfLength;
    const float centreX = surface.world[12];
    const float centreZ = surface.world[14];
    return tile.originX + tile.size >= centreX - extentX
        && tile.originX <= centreX + extentX
        && tile.originZ + tile.size >= centreZ - extentZ
        && tile.originZ <= centreZ + extentZ;
}

/** The sun this surface reflects: the first directional light, if any. */
const Concord::RenderLight* SunLight(const Concord::RenderLight* lights,
                                     std::uint32_t count) noexcept
{
    if (lights == nullptr) {
        return nullptr;
    }
    const int caster = Concord::FindShadowCastingLight(lights, count);
    if (caster >= 0) {
        return &lights[static_cast<std::uint32_t>(caster)];
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (lights[index].type == Concord::LightType::Directional) {
            return &lights[index];
        }
    }
    return nullptr;
}

} // namespace

namespace Concord {

bool BgfxWaterRenderer::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    if (m_attempted) {
        return false;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kWaterShaders, type, "vs_water");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kWaterShaders, type, "fs_water");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "water shader creation failed");
        if (bgfx::isValid(vs)) { bgfx::destroy(vs); }
        if (bgfx::isValid(fs)) { bgfx::destroy(fs); }
        return false;
    }
    m_program = bgfx::createProgram(vs, fs, true);
    m_uSurface = bgfx::createUniform("u_waterSurface", bgfx::UniformType::Vec4);
    m_uFlow = bgfx::createUniform("u_waterFlow", bgfx::UniformType::Vec4);
    m_uCamera = bgfx::createUniform("u_waterCamera", bgfx::UniformType::Vec4);
    m_uShallow = bgfx::createUniform("u_waterShallow", bgfx::UniformType::Vec4);
    m_uDeep = bgfx::createUniform("u_waterDeep", bgfx::UniformType::Vec4);
    m_uOptics = bgfx::createUniform("u_waterOptics", bgfx::UniformType::Vec4);
    m_uSunDir = bgfx::createUniform("u_waterSunDir", bgfx::UniformType::Vec4);
    m_uSunColor = bgfx::createUniform("u_waterSunColor", bgfx::UniformType::Vec4);
    m_uSkyZenith = bgfx::createUniform("u_waterSkyZenith", bgfx::UniformType::Vec4);
    m_uSkyHorizon = bgfx::createUniform("u_waterSkyHorizon", bgfx::UniformType::Vec4);
    m_uAmbient = bgfx::createUniform("u_waterAmbient", bgfx::UniformType::Vec4);
    m_uDepthParams = bgfx::createUniform("u_waterDepthParams", bgfx::UniformType::Vec4);
    m_uAdvanced = bgfx::createUniform("u_waterAdvanced", bgfx::UniformType::Vec4);
    m_sSceneColor = bgfx::createUniform("s_waterScene", bgfx::UniformType::Sampler);
    m_sSceneDepth = bgfx::createUniform("s_waterSceneDepth", bgfx::UniformType::Sampler);
    m_uPlanarViewProj = bgfx::createUniform("u_waterPlanarViewProj", bgfx::UniformType::Mat4);
    m_uPlanarParams = bgfx::createUniform("u_waterPlanarParams", bgfx::UniformType::Vec4);
    m_sPlanar = bgfx::createUniform("s_waterPlanar", bgfx::UniformType::Sampler);
    m_uCascadeParams = bgfx::createUniform("u_waterCascadeParams",
                                           bgfx::UniformType::Vec4, WaterCascade::kLevels);
    m_uWaveA = bgfx::createUniform("u_waterWaveA", bgfx::UniformType::Vec4,
                                   Water::kMaxWaterWaves);
    m_uWaveB = bgfx::createUniform("u_waterWaveB", bgfx::UniformType::Vec4,
                                   Water::kMaxWaterWaves);
    m_uTile = bgfx::createUniform("u_waterTile", bgfx::UniformType::Vec4);
    m_sCascade = bgfx::createUniform("s_waterCascade", bgfx::UniformType::Sampler);

    m_ready = bgfx::isValid(m_program) && bgfx::isValid(m_uSurface)
        && bgfx::isValid(m_uFlow) && bgfx::isValid(m_uCamera)
        && bgfx::isValid(m_uShallow) && bgfx::isValid(m_uDeep)
        && bgfx::isValid(m_uOptics) && bgfx::isValid(m_uSunDir)
        && bgfx::isValid(m_uSunColor) && bgfx::isValid(m_uSkyZenith)
        && bgfx::isValid(m_uSkyHorizon) && bgfx::isValid(m_uAmbient)
        && bgfx::isValid(m_uDepthParams) && bgfx::isValid(m_uAdvanced)
        && bgfx::isValid(m_sSceneColor)
        && bgfx::isValid(m_sSceneDepth) && bgfx::isValid(m_uPlanarViewProj)
        && bgfx::isValid(m_uPlanarParams) && bgfx::isValid(m_sPlanar)
        && bgfx::isValid(m_uCascadeParams) && bgfx::isValid(m_uTile)
        && bgfx::isValid(m_uWaveA) && bgfx::isValid(m_uWaveB)
        && bgfx::isValid(m_sCascade)
        && m_cascade.EnsureReady()
        && m_clipmap.EnsureReady() && m_grids.EnsureReady();
    if (!m_ready) {
        Debug::Logger::Error("Render", "water pass resource init failed");
        DestroyResources();
    }
    return m_ready;
}

void BgfxWaterRenderer::DestroyResources()
{
    for (bgfx::UniformHandle* handle : {&m_uSurface, &m_uFlow,
                                        &m_uCamera, &m_uShallow, &m_uDeep, &m_uOptics,
                                       &m_uSunDir, &m_uSunColor, &m_uSkyZenith,
                                       &m_uSkyHorizon, &m_uAmbient, &m_uDepthParams,
                                       &m_uAdvanced,
                                       &m_sSceneColor, &m_sSceneDepth,
                                       &m_uPlanarViewProj, &m_uPlanarParams,
                                       &m_sPlanar, &m_uCascadeParams, &m_uTile,
                                       &m_sCascade, &m_uWaveA, &m_uWaveB}) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_cascade.Shutdown();
    m_clipmap.Shutdown();
    m_grids.Shutdown();
    m_ready = false;
}

void BgfxWaterRenderer::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void BgfxWaterRenderer::ApplySurface(const RenderWaterSurface& surface,
                                     const SkyEnvironment& environment,
                                     const DrawParams& params)
{
    // The vertex stage sums the CPU-resolved Gerstner octaves, so the drawn
    // surface and buoyancy queries agree; y carries the active wave count.
    const std::uint32_t waveCount =
        std::min(surface.waves.count, Water::kMaxWaterWaves);
    const float surfaceParams[4] = {
        surface.state.time, static_cast<float>(waveCount),
        surface.motion == Water::WaterMotion::Still ? 0.0f : 1.0f,
        surface.world[13]};
    bgfx::setUniform(m_uSurface, surfaceParams);

    float waveA[Water::kMaxWaterWaves][4]{};
    float waveB[Water::kMaxWaterWaves][4]{};
    for (std::uint32_t index = 0; index < waveCount; ++index) {
        const Water::GerstnerWave& wave = surface.waves.waves[index];
        const float wavelength = std::max(wave.wavelength, 1e-3f);
        const float k = 6.2831853f / wavelength;
        // Normalised horizontal sharpness: at 1 the summed crests still cannot
        // fold the surface over itself (same rule the CPU sampler applies).
        const float q = wave.amplitude > 1e-5f && waveCount > 0
            ? std::clamp(wave.steepness, 0.0f, 1.0f)
                / (k * wave.amplitude * static_cast<float>(waveCount))
            : 0.0f;
        waveA[index][0] = wave.directionX;
        waveA[index][1] = wave.directionZ;
        waveA[index][2] = wave.amplitude;
        waveA[index][3] = wavelength;
        waveB[index][0] = wave.speed;
        waveB[index][1] = q;
    }
    bgfx::setUniform(m_uWaveA, waveA, Water::kMaxWaterWaves);
    bgfx::setUniform(m_uWaveB, waveB, Water::kMaxWaterWaves);
    bgfx::setUniform(m_uCascadeParams, m_cascade.LevelParams(), WaterCascade::kLevels);
    const float flow[4] = {surface.state.flowOffsetX, surface.state.flowOffsetZ,
                           surface.flowVelocity[0], surface.flowVelocity[1]};
    bgfx::setUniform(m_uFlow, flow);

    const float camera[4] = {params.eye[0], params.eye[1], params.eye[2],
                             std::max(surface.refractionStrength, 0.0f)};
    bgfx::setUniform(m_uCamera, camera);

    float shallow[4]{};
    UnpackLinear(surface.shallowColor, shallow);
    shallow[3] = surface.absorption;
    bgfx::setUniform(m_uShallow, shallow);
    float deep[4]{};
    UnpackLinear(surface.deepColor, deep);
    deep[3] = surface.depth;
    bgfx::setUniform(m_uDeep, deep);

    const float optics[4] = {std::max(surface.roughness, 0.01f), surface.foamWidth,
                             surface.foamIntensity,
                             surface.kind == Water::WaterKind::River ? 1.0f
                             : (surface.kind == Water::WaterKind::Ocean ? 2.0f : 0.0f)};
    bgfx::setUniform(m_uOptics, optics);

    // Scattering tint packed as 0xRRGGBB into the advanced uniform's w. The
    // fragment shader unpacks it back to linear RGB (see fs_water's
    // UnpackScatteringTint); packing avoids a fifth uniform for one colour.
    const std::uint32_t scatteringPacked =
        (surface.state.optics.scatteringColor >> 24) << 16
        | ((surface.state.optics.scatteringColor >> 16) & 0xffu) << 8
        | (surface.state.optics.scatteringColor >> 8) & 0xffu;
    const float advanced[4] = {
        std::clamp(surface.state.optics.subsurfaceScattering, 0.0f, 1.0f),
        std::clamp(surface.state.optics.foamGrainScale, 0.3f, 2.5f),
        std::clamp(surface.state.optics.sunGlintIntensity, 0.0f, 2.0f),
        static_cast<float>(scatteringPacked)};
    bgfx::setUniform(m_uAdvanced, advanced);

    const RenderLight* sun = SunLight(params.lights, params.lightCount);
    float sunDir[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    float sunColor[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    if (sun != nullptr) {
        sunDir[0] = sun->direction[0];
        sunDir[1] = sun->direction[1];
        sunDir[2] = sun->direction[2];
        sunDir[3] = std::max(sun->intensity, 0.0f);
        UnpackLinear(sun->color, sunColor);
    }
    bgfx::setUniform(m_uSunDir, sunDir);
    bgfx::setUniform(m_uSunColor, sunColor);

    // Reflect the same zenith/horizon ramp the procedural sky is drawn with, so
    // the water and the sky above it never disagree on colour.
    float zenith[4]{};
    UnpackLinear(environment.zenithColor, zenith);
    zenith[3] = std::max(environment.intensity, 0.0f);
    bgfx::setUniform(m_uSkyZenith, zenith);
    float horizon[4]{};
    UnpackLinear(environment.horizonColor, horizon);
    bgfx::setUniform(m_uSkyHorizon, horizon);

    // Indirect fill, from the same authored ambient the meshes are lit by: the
    // authored water colours are albedo, so without it the body has no light to
    // return and renders black.
    float ambient[4]{};
    UnpackLinear(environment.ambientColor, ambient);
    ambient[3] = std::max(environment.ambientIntensity, 0.0f);
    bgfx::setUniform(m_uAmbient, ambient);
}

void BgfxWaterRenderer::Draw(const DrawParams& params, const SkyEnvironment& environment,
                             const RenderWaterSurface* surfaces,
                             std::uint32_t surfaceCount)
{
    if (surfaces == nullptr || surfaceCount == 0 || params.eye == nullptr
        || params.viewMatrix == nullptr || params.projectionMatrix == nullptr
        || params.view == kInvalidRenderView || params.width == 0 || params.height == 0
        || !EnsureReady()) {
        return;
    }

    std::uint32_t drawn = std::min(surfaceCount, kMaxRenderWaterSurfaces);
    if (surfaceCount > kMaxRenderWaterSurfaces) {
        Debug::Logger::Debug("Render", "water: %u surfaces exceed the cap of %u",
                             surfaceCount, kMaxRenderWaterSurfaces);
    }

    bgfx::setViewFrameBuffer(params.view, params.sceneFb);
    bgfx::setViewRect(params.view, 0, 0, static_cast<std::uint16_t>(params.width),
                      static_cast<std::uint16_t>(params.height));
    // No clear: the pass composites onto the resolved opaque scene already in
    // the target, and shares its depth buffer.
    bgfx::setViewTransform(params.view, params.viewMatrix, params.projectionMatrix);
    bgfx::setViewMode(params.view, bgfx::ViewMode::Sequential);
    bgfx::touch(params.view);

    // Snapshot the resolved scene before the first surface draws. Blits queued on
    // a view run before that view's draws, so the copies hold pure opaque scene:
    // what the water refracts, and how far away its bed is.
    const bool refract = bgfx::isValid(params.sceneColor) && bgfx::isValid(params.sceneDepth)
        && bgfx::isValid(params.sceneColorCopy) && bgfx::isValid(params.sceneDepthCopy)
        && (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) != 0;
    if (refract) {
        bgfx::blit(params.view, params.sceneColorCopy, 0, 0, params.sceneColor);
        bgfx::blit(params.view, params.sceneDepthCopy, 0, 0, params.sceneDepth);
    }
    // Bake the wave cascade for the first moving surface: one shared atlas,
    // rebaked each frame on the bake view the backend ordered before this one,
    // gives the fragment stage band-limited spectrum normals and fold-driven
    // foam that the per-vertex Gerstner sum cannot carry.
    bool cascadeBaked = false;
    if (params.bakeView != kInvalidRenderView) {
        for (std::uint32_t index = 0; index < drawn; ++index) {
            if (surfaces[index].motion != Water::WaterMotion::Still) {
                m_cascade.Update(params.bakeView, surfaces[index],
                                 params.eye[0], params.eye[2]);
                cascadeBaked = true;
                break;
            }
        }
    }
    // Real-scene reflection, when the backend ran the mirrored camera for this
    // water plane. Without it the surface only has the analytic sky to reflect,
    // which is the main reason water reads as painted rather than wet.
    const bool planar = bgfx::isValid(params.planarColor) && params.planarViewProj != nullptr;
    const float planarParams[4] = {planar ? 1.0f : 0.0f,
                                   params.flipPlanarV ? 1.0f : 0.0f,
                                   cascadeBaked ? 1.0f : 0.0f, 0.0f};
    const float depthParams[4] = {
        std::max(params.nearPlane, 1e-3f), std::max(params.farPlane, params.nearPlane + 1e-3f),
        bgfx::getCaps()->homogeneousDepth ? 1.0f : 0.0f, refract ? 1.0f : 0.0f};

    // Deliberately two-sided. Face culling on a displaced surface removes the
    // far slope of every wave, which reads as stripes rather than water, and it
    // deletes the whole surface when the camera drops below it. The fragment
    // shader orients the normal toward the viewer instead, so both sides shade.
    const std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_BLEND_ALPHA;

    for (std::uint32_t index = 0; index < drawn; ++index) {
        const RenderWaterSurface& surface = surfaces[index];
        // one flat plane per surface, transformed by the surface's real world
        // matrix plus a width/length scale. Using the full matrix keeps the
        // plane aligned with the authored node transform instead of assuming an
        // axis-aligned XZ rectangle, which is what produced the bogus vertical
        // reflective slab when the simplified path drifted out of sync.
        const WaterGridMesh::Grid* grid = m_grids.Acquire(
            std::max(surface.tessellation, 1u));
        if (grid == nullptr) {
            continue;
        }
        float sizeScale[16];
        bx::mtxScale(sizeScale, surface.width, 1.0f, surface.length);
        float world[16];
        Multiply(world, surface.world, sizeScale);
        constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;
        if (bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
            continue;
        }
        bgfx::InstanceDataBuffer instances;
        bgfx::allocInstanceDataBuffer(&instances, 1, kInstanceStride);
        std::memcpy(instances.data, world, sizeof(world));

        ApplySurface(surface, environment, params);
        bgfx::setUniform(m_uPlanarParams, planarParams);
        if (planar) {
            bgfx::setUniform(m_uPlanarViewProj, params.planarViewProj);
            bgfx::setTexture(2, m_sPlanar, params.planarColor);
        }
        if (refract) {
            bgfx::setTexture(0, m_sSceneColor, params.sceneColorCopy);
            bgfx::setTexture(1, m_sSceneDepth, params.sceneDepthCopy);
        }
        bgfx::setTexture(3, m_sCascade, m_cascade.Atlas());
        bgfx::setUniform(m_uDepthParams, depthParams);
        bgfx::setState(state);
        bgfx::setVertexBuffer(0, grid->vertices);
        bgfx::setIndexBuffer(grid->indices);
        bgfx::setInstanceDataBuffer(&instances);
        bgfx::submit(params.view, m_program);
    }
}

} // namespace Concord
