#include "engine/render/particles/BgfxGpuParticleRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/particles/ParticleSimulationRuntime.h"
#include "engine/render/backend/BgfxMathConverters.h"
#include "engine/render/shaders/generated/cs_gpu_particle_simulate.bin.h"
#include "engine/render/shaders/generated/fs_gpu_particle_billboard.bin.h"
#include "engine/render/shaders/generated/vs_gpu_particle_billboard.bin.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

const bgfx::EmbeddedShader kGpuParticleShaders[] = {
    {
        "cs_gpu_particle_simulate",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, cs_gpu_particle_simulate)
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "vs_gpu_particle_billboard",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_gpu_particle_billboard)
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "fs_gpu_particle_billboard",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_gpu_particle_billboard)
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

struct QuadVertex {
    float position[3];
    float uv[2];
};

constexpr QuadVertex kQuadVertices[] = {
    {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
    {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
    {{ 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f}},
    {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}},
};

constexpr std::uint16_t kQuadIndices[] = {0, 1, 2, 0, 2, 3};

float UIntAsFloat(std::uint32_t value) noexcept
{
    return std::bit_cast<float>(value);
}

std::uint32_t LowBits(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(value & 0xffffffffu);
}

bool Finite(float value) noexcept
{
    return std::isfinite(value);
}

float FiniteOr(float value, float fallback) noexcept
{
    return Finite(value) ? value : fallback;
}

void BuildRotationMatrices(float rotation[16], float inverseRotation[16],
                           const float world[16])
{
    for (std::size_t index = 0; index < 16; ++index) {
        if (!std::isfinite(world[index])) {
            bx::mtxIdentity(rotation);
            bx::mtxIdentity(inverseRotation);
            return;
        }
    }
    const float xLength = std::sqrt(
        world[0] * world[0] + world[1] * world[1] + world[2] * world[2]);
    const float yLength = std::sqrt(
        world[4] * world[4] + world[5] * world[5] + world[6] * world[6]);
    const float zLength = std::sqrt(
        world[8] * world[8] + world[9] * world[9] + world[10] * world[10]);
    if (xLength <= 1e-6f || yLength <= 1e-6f || zLength <= 1e-6f) {
        bx::mtxIdentity(rotation);
        bx::mtxIdentity(inverseRotation);
        return;
    }

    bx::mtxIdentity(rotation);
    rotation[0] = world[0] / xLength;
    rotation[1] = world[1] / xLength;
    rotation[2] = world[2] / xLength;
    rotation[4] = world[4] / yLength;
    rotation[5] = world[5] / yLength;
    rotation[6] = world[6] / yLength;
    rotation[8] = world[8] / zLength;
    rotation[9] = world[9] / zLength;
    rotation[10] = world[10] / zLength;

    bx::mtxIdentity(inverseRotation);
    inverseRotation[0] = rotation[0];
    inverseRotation[1] = rotation[4];
    inverseRotation[2] = rotation[8];
    inverseRotation[4] = rotation[1];
    inverseRotation[5] = rotation[5];
    inverseRotation[6] = rotation[9];
    inverseRotation[8] = rotation[2];
    inverseRotation[9] = rotation[6];
    inverseRotation[10] = rotation[10];
}

} // namespace

namespace Concord {

BgfxGpuParticleRenderer::~BgfxGpuParticleRenderer()
{
    Shutdown();
}

std::size_t BgfxGpuParticleRenderer::EmitterKeyHash::operator()(
    const EmitterKey& key) const noexcept
{
    const std::size_t a = std::hash<std::uintptr_t>{}(key.emitterKey);
    const std::size_t b = std::hash<RenderViewHandle>{}(key.ownerView);
    return a ^ (b + 0x9e3779b9u + (a << 6u) + (a >> 2u));
}

bool BgfxGpuParticleRenderer::Supported() const
{
    const std::uint64_t supported = bgfx::getCaps()->supported;
    return (supported & BGFX_CAPS_COMPUTE) != 0
        && (supported & BGFX_CAPS_INSTANCING) != 0;
}

bool BgfxGpuParticleRenderer::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;
    if (!Supported()) {
        Debug::Logger::Warn("Render", "GPU particles require compute and instancing support");
        return false;
    }
    m_ready = CreateSharedResources();
    if (!m_ready) {
        Debug::Logger::Error("Render", "GPU particle resource initialization failed");
        Shutdown();
        m_attempted = true;
    }
    return m_ready;
}

bool BgfxGpuParticleRenderer::CreateSharedResources()
{
    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle compute = bgfx::createEmbeddedShader(
        kGpuParticleShaders, type, "cs_gpu_particle_simulate");
    const bgfx::ShaderHandle vertex = bgfx::createEmbeddedShader(
        kGpuParticleShaders, type, "vs_gpu_particle_billboard");
    const bgfx::ShaderHandle fragment = bgfx::createEmbeddedShader(
        kGpuParticleShaders, type, "fs_gpu_particle_billboard");
    if (!bgfx::isValid(compute) || !bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        if (bgfx::isValid(compute)) {
            bgfx::destroy(compute);
        }
        if (bgfx::isValid(vertex)) {
            bgfx::destroy(vertex);
        }
        if (bgfx::isValid(fragment)) {
            bgfx::destroy(fragment);
        }
        return false;
    }

    m_computeProgram = bgfx::createProgram(compute, true);
    m_drawProgram = bgfx::createProgram(vertex, fragment, true);
    m_uSimulation = bgfx::createUniform(
        "u_gpuParticleSimulation", bgfx::UniformType::Vec4, 12);
    m_uForceFields = bgfx::createUniform(
        "u_gpuParticleForceFields", bgfx::UniformType::Vec4,
        kMaxRenderParticleForceFields * 2);
    m_uEmitterWorld = bgfx::createUniform("u_gpuParticleWorld", bgfx::UniformType::Mat4);
    m_uEmitterRotation = bgfx::createUniform(
        "u_gpuParticleRotation", bgfx::UniformType::Mat4);
    m_uEmitterInverseRotation = bgfx::createUniform(
        "u_gpuParticleInverseRotation", bgfx::UniformType::Mat4);
    m_uClipPlane = bgfx::createUniform(
        "u_gpuParticleClipPlane", bgfx::UniformType::Vec4);
    m_uVisual = bgfx::createUniform("u_gpuParticleVisual", bgfx::UniformType::Vec4);
    m_uDraw = bgfx::createUniform("u_gpuParticleDraw", bgfx::UniformType::Vec4);
    m_uColorStart = bgfx::createUniform("u_gpuParticleColorStart", bgfx::UniformType::Vec4);
    m_uColorMid = bgfx::createUniform("u_gpuParticleColorMid", bgfx::UniformType::Vec4);
    m_uColorEnd = bgfx::createUniform("u_gpuParticleColorEnd", bgfx::UniformType::Vec4);

    bgfx::VertexLayout quadLayout;
    quadLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    m_quadVertexBuffer = bgfx::createVertexBuffer(
        bgfx::makeRef(kQuadVertices, sizeof(kQuadVertices)), quadLayout);
    m_quadIndexBuffer = bgfx::createIndexBuffer(
        bgfx::makeRef(kQuadIndices, sizeof(kQuadIndices)));

    m_particleLayout.begin()
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
        .end();

    return bgfx::isValid(m_computeProgram) && bgfx::isValid(m_drawProgram)
        && bgfx::isValid(m_uSimulation) && bgfx::isValid(m_uForceFields)
        && bgfx::isValid(m_uEmitterWorld) && bgfx::isValid(m_uEmitterRotation)
        && bgfx::isValid(m_uEmitterInverseRotation) && bgfx::isValid(m_uClipPlane)
        && bgfx::isValid(m_uVisual) && bgfx::isValid(m_uDraw)
        && bgfx::isValid(m_uColorStart)
        && bgfx::isValid(m_uColorMid) && bgfx::isValid(m_uColorEnd)
        && bgfx::isValid(m_quadVertexBuffer) && bgfx::isValid(m_quadIndexBuffer);
}

bool BgfxGpuParticleRenderer::EnsureEmitterState(EmitterState& state,
                                                  std::uint32_t capacity)
{
    capacity = std::clamp(capacity, 1u, Particles::kMaxGpuParticleCapacity);
    if (bgfx::isValid(state.particles) && state.capacity == capacity) {
        return true;
    }
    DestroyEmitterState(state);
    state.particles = bgfx::createDynamicVertexBuffer(
        capacity, m_particleLayout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(state.particles)) {
        Debug::Logger::Error(
            "Render", "GPU particle state allocation failed; switching emitters to CPU");
        Particles::ParticleSimulationRuntime::SetGpuAvailability(
            Particles::GpuParticleAvailability::Unavailable);
        return false;
    }
    state.capacity = capacity;
    return true;
}

void BgfxGpuParticleRenderer::DestroyEmitterState(EmitterState& state)
{
    if (bgfx::isValid(state.particles)) {
        bgfx::destroy(state.particles);
    }
    state = {};
}

void BgfxGpuParticleRenderer::Simulate(
    RenderViewHandle ownerView, RenderViewHandle computeView,
    const std::vector<RenderParticleEmitter>& emitters)
{
    if (!EnsureReady() || computeView == kInvalidRenderView) {
        return;
    }
    bgfx::setViewName(computeView, "GPU particles: simulate");
    bgfx::setViewMode(computeView, bgfx::ViewMode::Sequential);

    constexpr double kLongRecoverySeconds = 0.5;
    constexpr double kStepSeconds = 1.0 / 30.0;
    constexpr std::uint32_t kMaxSteps = 8;

    for (const RenderParticleEmitter& emitter : emitters) {
        if (!emitter.descriptor.billboard || emitter.emitterKey == 0
            || emitter.descriptor.capacity == 0) {
            continue;
        }
        const EmitterKey key{ownerView, emitter.emitterKey};
        EmitterState& state = m_emitters[key];
        if (!EnsureEmitterState(state, emitter.descriptor.capacity)) {
            continue;
        }
        state.lastSeenFrame = m_frameNumber;

        const bool firstPacket = !state.initialized;
        const bool reset = !state.initialized
            || state.resetGeneration != emitter.resetGeneration
            || emitter.spawnSequence < state.spawnSequence
            || emitter.simulationTime < state.simulationTime;
        if (reset) {
            state.resetGeneration = emitter.resetGeneration;
            state.spawnSequence = 0;
            state.simulationTime = 0.0;
        }

        const std::uint64_t missingSpawns = emitter.spawnSequence - state.spawnSequence;
        const std::uint64_t retainedSpawns = std::min<std::uint64_t>(
            missingSpawns, state.capacity);
        const std::uint64_t spawnBegin = emitter.spawnSequence - retainedSpawns;
        const double elapsed = reset
            ? std::max(0.0, emitter.simulationTime)
            : std::max(0.0, emitter.simulationTime - state.simulationTime);
        double maximumLifetime = std::max(
            0.05, static_cast<double>(std::max(
                emitter.descriptor.lifetimeMin, emitter.descriptor.lifetimeMax)));
        if (!std::isfinite(maximumLifetime)) {
            maximumLifetime = 0.05;
        }
        const float recoveredPrewarm = firstPacket
            && emitter.prewarmSeconds <= 0.0f
            && emitter.descriptor.prewarm
            && emitter.descriptor.duration > 0.0f
                ? std::min(emitter.descriptor.duration,
                           static_cast<float>(maximumLifetime))
                : emitter.prewarmSeconds;

        if (elapsed > kLongRecoverySeconds) {
            const double integrationTime = std::min(elapsed, maximumLifetime);
            const std::uint32_t integrationSteps = integrationTime > 0.0
                ? static_cast<std::uint32_t>(std::clamp(
                      std::ceil(integrationTime / kStepSeconds), 1.0,
                      static_cast<double>(kMaxSteps)))
                : 1u;
            for (std::uint32_t step = 0; step < integrationSteps; ++step) {
                DispatchStep(
                    computeView, state, emitter,
                    static_cast<float>(integrationTime / integrationSteps),
                    spawnBegin, 0, reset && step == 0, 0.0f);
            }
            const float spawnHistory = std::max(
                recoveredPrewarm,
                static_cast<float>(std::min(elapsed, maximumLifetime)));
            const std::uint64_t latestSpawns = recoveredPrewarm <= 0.0f
                ? std::min<std::uint64_t>(retainedSpawns, emitter.spawnCount)
                : 0u;
            const std::uint64_t historicalSpawns = retainedSpawns - latestSpawns;
            if (historicalSpawns > 0) {
                DispatchStep(
                    computeView, state, emitter, 0.0f, spawnBegin,
                    static_cast<std::uint32_t>(historicalSpawns),
                    false, spawnHistory);
            }
            if (latestSpawns > 0) {
                DispatchStep(
                    computeView, state, emitter, 0.0f,
                    spawnBegin + historicalSpawns,
                    static_cast<std::uint32_t>(latestSpawns), false, 0.0f);
            }
        } else {
            std::uint32_t steps = elapsed > 0.0
                ? static_cast<std::uint32_t>(std::ceil(elapsed / kStepSeconds))
                : 1u;
            steps = std::clamp(steps, 1u, kMaxSteps);
            std::uint64_t dispatchedSpawns = 0;
            for (std::uint32_t step = 0; step < steps; ++step) {
                const std::uint64_t target = retainedSpawns * (step + 1u) / steps;
                const std::uint32_t stepSpawns = static_cast<std::uint32_t>(
                    target - dispatchedSpawns);
                DispatchStep(
                    computeView, state, emitter,
                    static_cast<float>(elapsed / steps),
                    spawnBegin + dispatchedSpawns, stepSpawns,
                    reset && step == 0, step == 0 ? recoveredPrewarm : 0.0f);
                dispatchedSpawns = target;
            }
        }

        state.resetGeneration = emitter.resetGeneration;
        state.spawnSequence = emitter.spawnSequence;
        state.simulationTime = emitter.simulationTime;
        state.initialized = true;
    }
}

void BgfxGpuParticleRenderer::DispatchStep(
    RenderViewHandle computeView, EmitterState& state,
    const RenderParticleEmitter& emitter, float deltaTime,
    std::uint64_t spawnBegin, std::uint32_t spawnCount,
    bool reset, float prewarmSeconds)
{
    const std::uint32_t spawnStart = static_cast<std::uint32_t>(
        spawnBegin % state.capacity);
    BindSimulationUniforms(emitter, state.capacity, deltaTime, spawnStart,
                           spawnCount, spawnBegin, reset, prewarmSeconds);
    bgfx::setBuffer(0, state.particles, bgfx::Access::ReadWrite);
    const std::uint32_t groups = (state.capacity + kThreadGroupSize - 1u)
        / kThreadGroupSize;
    bgfx::dispatch(computeView, m_computeProgram, groups, 1, 1);
}

void BgfxGpuParticleRenderer::BindSimulationUniforms(
    const RenderParticleEmitter& emitter, std::uint32_t capacity,
    float deltaTime, std::uint32_t spawnStart, std::uint32_t spawnCount,
    std::uint64_t spawnBegin, bool reset, float prewarmSeconds)
{
    const Particles::ParticleEmitterDesc& desc = emitter.descriptor;
    float params[12][4]{};
    params[0][0] = std::max(0.0f, FiniteOr(deltaTime, 0.0f));
    params[0][1] = UIntAsFloat(capacity);
    params[0][2] = UIntAsFloat(spawnStart);
    params[0][3] = UIntAsFloat(std::min(spawnCount, capacity));
    params[1][0] = UIntAsFloat(desc.seed ? desc.seed : 1u);
    params[1][1] = UIntAsFloat(static_cast<std::uint32_t>(desc.shape));
    params[1][2] = UIntAsFloat(desc.localSpace ? 1u : 0u);
    params[1][3] = UIntAsFloat(reset ? 1u : 0u);
    params[2][0] = std::max(0.05f, FiniteOr(desc.lifetimeMin, 0.05f));
    params[2][1] = std::max(0.05f, FiniteOr(desc.lifetimeMax, 0.05f));
    params[2][2] = static_cast<float>(std::fmod(
        std::max(0.0, emitter.simulationTime), 4096.0));
    params[2][3] = std::max(0.0f, FiniteOr(prewarmSeconds, 0.0f));
    params[3][0] = FiniteOr(desc.direction.x, 0.0f);
    params[3][1] = FiniteOr(desc.direction.y, 1.0f);
    params[3][2] = FiniteOr(desc.direction.z, 0.0f);
    params[3][3] = FiniteOr(desc.speedMin, 0.0f);
    params[4][0] = FiniteOr(desc.gravity.x, 0.0f);
    params[4][1] = FiniteOr(desc.gravity.y, 0.0f);
    params[4][2] = FiniteOr(desc.gravity.z, 0.0f);
    params[4][3] = FiniteOr(desc.speedMax, params[3][3]);
    params[5][0] = std::abs(FiniteOr(desc.shapeSize.x, 0.0f));
    params[5][1] = std::abs(FiniteOr(desc.shapeSize.y, 0.0f));
    params[5][2] = std::abs(FiniteOr(desc.shapeSize.z, 0.0f));
    params[5][3] = std::clamp(FiniteOr(desc.shapeAngleDegrees, 0.0f), 0.0f, 180.0f)
        * bx::kPi / 180.0f;
    params[6][0] = std::clamp(FiniteOr(desc.spreadDegrees, 0.0f), 0.0f, 180.0f)
        * bx::kPi / 180.0f;
    params[6][1] = std::clamp(FiniteOr(desc.drag, 0.0f), 0.0f, 1.0f);
    params[6][2] = std::max(0.0f, FiniteOr(desc.maxSpeed, 0.0f));
    params[6][3] = std::clamp(FiniteOr(desc.inheritEmitterVelocity, 0.0f), 0.0f, 1.0f);
    params[7][0] = FiniteOr(emitter.emitterVelocity.x, 0.0f);
    params[7][1] = FiniteOr(emitter.emitterVelocity.y, 0.0f);
    params[7][2] = FiniteOr(emitter.emitterVelocity.z, 0.0f);
    params[7][3] = UIntAsFloat(!desc.localSpace && Finite(desc.groundY) ? 1u : 0u);
    params[8][0] = FiniteOr(desc.groundY, 0.0f);
    params[8][1] = std::clamp(FiniteOr(desc.bounce, 0.0f), 0.0f, 1.0f);
    params[8][2] = std::clamp(FiniteOr(desc.groundFriction, 1.0f), 0.0f, 1.0f);
    params[8][3] = std::max(0.0f, FiniteOr(desc.turbulenceStrength, 0.0f));
    params[9][0] = std::max(0.0f, FiniteOr(desc.turbulenceFrequency, 0.0f));
    params[9][1] = UIntAsFloat(static_cast<std::uint32_t>(
        std::min<std::size_t>(desc.forceFields.size(),
                              kMaxRenderParticleForceFields)));
    params[9][2] = UIntAsFloat(LowBits(spawnBegin));
    params[10][0] = FiniteOr(desc.rotationVelocityMin.x, 0.0f);
    params[10][1] = FiniteOr(desc.rotationVelocityMin.y, 0.0f);
    params[10][2] = FiniteOr(desc.rotationVelocityMin.z, 0.0f);
    params[11][0] = FiniteOr(desc.rotationVelocityMax.x, 0.0f);
    params[11][1] = FiniteOr(desc.rotationVelocityMax.y, 0.0f);
    params[11][2] = FiniteOr(desc.rotationVelocityMax.z, 0.0f);
    bgfx::setUniform(m_uSimulation, params, 12);

    float fields[kMaxRenderParticleForceFields * 2][4]{};
    const std::size_t fieldCount = std::min<std::size_t>(
        desc.forceFields.size(), kMaxRenderParticleForceFields);
    for (std::size_t i = 0; i < fieldCount; ++i) {
        const Particles::ParticleForceField& field = desc.forceFields[i];
        fields[i * 2][0] = FiniteOr(field.position.x, 0.0f);
        fields[i * 2][1] = FiniteOr(field.position.y, 0.0f);
        fields[i * 2][2] = FiniteOr(field.position.z, 0.0f);
        fields[i * 2][3] = UIntAsFloat(static_cast<std::uint32_t>(field.type));
        fields[i * 2 + 1][0] = FiniteOr(field.strength, 0.0f);
        fields[i * 2 + 1][1] = std::max(0.0f, FiniteOr(field.radius, 0.0f));
    }
    bgfx::setUniform(m_uForceFields, fields,
                     kMaxRenderParticleForceFields * 2);
    bgfx::setUniform(m_uEmitterWorld, emitter.world);
    float rotation[16];
    float inverseRotation[16];
    BuildRotationMatrices(rotation, inverseRotation, emitter.world);
    bgfx::setUniform(m_uEmitterRotation, rotation);
    bgfx::setUniform(m_uEmitterInverseRotation, inverseRotation);
}

void BgfxGpuParticleRenderer::BindDrawUniforms(
    const RenderParticleEmitter& emitter, const float clipPlane[4])
{
    const Particles::ParticleEmitterDesc& desc = emitter.descriptor;
    const float visual[4] = {
        std::max(0.0f, FiniteOr(desc.sizeStart, 0.0f)),
        std::max(0.0f, FiniteOr(desc.sizeMid, 0.0f)),
        std::max(0.0f, FiniteOr(desc.sizeEnd, 0.0f)),
        std::max(0.0f, FiniteOr(desc.brightness, 0.0f)),
    };
    const float draw[4] = {
        desc.localSpace ? 1.0f : 0.0f,
        static_cast<float>(desc.blend),
        0.0f,
        0.0f,
    };
    float colorStart[4];
    float colorMid[4];
    float colorEnd[4];
    RenderDetail::ColorToFloat4(colorStart, desc.colorStart);
    RenderDetail::ColorToFloat4(colorMid, desc.colorMid);
    RenderDetail::ColorToFloat4(colorEnd, desc.colorEnd);
    bgfx::setUniform(m_uVisual, visual);
    bgfx::setUniform(m_uDraw, draw);
    bgfx::setUniform(m_uColorStart, colorStart);
    bgfx::setUniform(m_uColorMid, colorMid);
    bgfx::setUniform(m_uColorEnd, colorEnd);
    bgfx::setUniform(m_uEmitterWorld, emitter.world);
    float rotation[16];
    float inverseRotation[16];
    BuildRotationMatrices(rotation, inverseRotation, emitter.world);
    bgfx::setUniform(m_uEmitterRotation, rotation);
    bgfx::setUniform(m_uEmitterInverseRotation, inverseRotation);
    const float disabledClip[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(m_uClipPlane, clipPlane != nullptr ? clipPlane : disabledClip);
}

void BgfxGpuParticleRenderer::Draw(
    RenderViewHandle ownerView, RenderViewHandle drawView,
    const std::vector<RenderParticleEmitter>& emitters,
    const float clipPlane[4])
{
    if (!m_ready || drawView == kInvalidRenderView) {
        return;
    }
    for (const RenderParticleEmitter& emitter : emitters) {
        if (!emitter.descriptor.billboard || emitter.emitterKey == 0) {
            continue;
        }
        const auto found = m_emitters.find({ownerView, emitter.emitterKey});
        if (found == m_emitters.end() || !found->second.initialized
            || !bgfx::isValid(found->second.particles)) {
            continue;
        }
        BindDrawUniforms(emitter, clipPlane);
        bgfx::setVertexBuffer(0, m_quadVertexBuffer);
        bgfx::setIndexBuffer(m_quadIndexBuffer);
        bgfx::setInstanceDataBuffer(found->second.particles, 0, found->second.capacity);
        std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_DEPTH_TEST_LEQUAL
            | RenderDetail::ToBgfxBlend(emitter.descriptor.blend);
        if (emitter.descriptor.blend == Material::BlendMode::Opaque) {
            state |= BGFX_STATE_WRITE_Z;
        }
        bgfx::setState(state);
        bgfx::submit(drawView, m_drawProgram);
    }
}

void BgfxGpuParticleRenderer::DestroyView(RenderViewHandle ownerView)
{
    for (auto it = m_emitters.begin(); it != m_emitters.end();) {
        if (it->first.ownerView == ownerView) {
            DestroyEmitterState(it->second);
            it = m_emitters.erase(it);
        } else {
            ++it;
        }
    }
}

void BgfxGpuParticleRenderer::EndFrame()
{
    ++m_frameNumber;
    constexpr std::uint64_t kRetireAfterFrames = 3;
    for (auto it = m_emitters.begin(); it != m_emitters.end();) {
        if (m_frameNumber - it->second.lastSeenFrame > kRetireAfterFrames) {
            DestroyEmitterState(it->second);
            it = m_emitters.erase(it);
        } else {
            ++it;
        }
    }
}

void BgfxGpuParticleRenderer::Shutdown()
{
    for (auto& [key, state] : m_emitters) {
        (void)key;
        DestroyEmitterState(state);
    }
    m_emitters.clear();
    for (bgfx::UniformHandle* uniform : {
             &m_uSimulation, &m_uForceFields, &m_uEmitterWorld,
             &m_uEmitterRotation, &m_uEmitterInverseRotation, &m_uClipPlane,
             &m_uVisual, &m_uDraw, &m_uColorStart,
             &m_uColorMid, &m_uColorEnd}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_quadVertexBuffer)) {
        bgfx::destroy(m_quadVertexBuffer);
        m_quadVertexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_quadIndexBuffer)) {
        bgfx::destroy(m_quadIndexBuffer);
        m_quadIndexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_computeProgram)) {
        bgfx::destroy(m_computeProgram);
        m_computeProgram = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_drawProgram)) {
        bgfx::destroy(m_drawProgram);
        m_drawProgram = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
    m_attempted = false;
    m_frameNumber = 1;
}

} // namespace Concord
