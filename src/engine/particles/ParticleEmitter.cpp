#include "engine/particles/ParticleEmitter.h"

#include "engine/debug/Logger.h"
#include "engine/particles/ParticleSimulationRuntime.h"
#include "engine/render/material/RenderMaterial.h"

#include <bx/math.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace Concord::Particles {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

std::uintptr_t NextGpuEmitterKey() noexcept
{
    static std::atomic<std::uintptr_t> next{1};
    std::uintptr_t key = next.fetch_add(1, std::memory_order_relaxed);
    while (key == 0) {
        key = next.fetch_add(1, std::memory_order_relaxed);
    }
    return key;
}

/** Cheap xorshift32; deterministic per-emitter without pulling <random>. */
std::uint32_t Xor32(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/** Random float in [0, 1). */
float RandUnit(std::uint32_t& state) noexcept
{
    // Top 24 bits give a uniform mantissa; divide by 2^24 to land in [0, 1).
    return (Xor32(state) >> 8) * (1.0f / 16777216.0f);
}

/** Random float in [a, b). */
float RandRange(float a, float b, std::uint32_t& state) noexcept
{
    return a + (b - a) * RandUnit(state);
}

/** Lerps two packed 0xRRGGBBAA colors component-wise; @p t is clamped to [0,1]. */
std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) noexcept
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    auto lane = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFFu);
        const float cb = static_cast<float>((b >> shift) & 0xFFu);
        const std::uint32_t out = static_cast<std::uint32_t>(ca + (cb - ca) * t + 0.5f);
        return (out & 0xFFu) << shift;
    };
    return lane(24) | lane(16) | lane(8) | lane(0);
}

/** 3-key color curve: start at t=0, mid at t=0.5, end at t=1. */
std::uint32_t SampleColor3(std::uint32_t s, std::uint32_t m, std::uint32_t e, float t) noexcept
{
    if (t <= 0.5f) {
        return LerpColor(s, m, t * 2.0f);
    }
    return LerpColor(m, e, (t - 0.5f) * 2.0f);
}

float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

/** 3-key scalar curve: start at t=0, mid at t=0.5, end at t=1. */
float SampleFloat3(float s, float m, float e, float t) noexcept
{
    if (t <= 0.5f) {
        return Lerp(s, m, t * 2.0f);
    }
    return Lerp(m, e, (t - 0.5f) * 2.0f);
}

/** Samples a unit vector inside a cone around `axis` with half-angle `spreadRad`. */
Vector3 SampleCone(const Vector3& axis, float spreadRad, std::uint32_t& state) noexcept
{
    // Normalise axis; fall back to +Y if the caller gave us zero.
    float ax = axis.x, ay = axis.y, az = axis.z;
    const float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (len < 1e-6f) { ax = 0.0f; ay = 1.0f; az = 0.0f; }
    else             { ax /= len; ay /= len; az /= len; }

    // Uniform sample on the cone cap (cos-weighted).
    const float cosMax = std::cos(spreadRad);
    const float cosTheta = cosMax + (1.0f - cosMax) * RandUnit(state);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = RandUnit(state) * 2.0f * kPi;
    const float lx = std::cos(phi) * sinTheta;
    const float ly = std::sin(phi) * sinTheta;
    const float lz = cosTheta;

    // Build any orthonormal basis with `axis` as +Z (Frisvad-style).
    Vector3 t{};
    if (std::fabs(az) < 0.999f) {
        const float sx = -ay, sy = ax, sz = 0.0f;
        const float sl = std::sqrt(sx * sx + sy * sy);
        t = Vector3{sx / sl, sy / sl, 0.0f};
    } else {
        t = Vector3{1.0f, 0.0f, 0.0f};
    }
    const Vector3 b{
        ay * t.z - az * t.y,
        az * t.x - ax * t.z,
        ax * t.y - ay * t.x,
    };
    return Vector3{
        t.x * lx + b.x * ly + ax * lz,
        t.y * lx + b.y * ly + ay * lz,
        t.z * lx + b.z * ly + az * lz,
    };
}

/** Uniform random point inside a ball of the given radius. */
Vector3 RandomInBall(float radius, std::uint32_t& state) noexcept
{
    // Rejection sampling on the unit cube gives a uniform ball distribution.
    for (int i = 0; i < 8; ++i) {
        const float x = RandRange(-1.0f, 1.0f, state);
        const float y = RandRange(-1.0f, 1.0f, state);
        const float z = RandRange(-1.0f, 1.0f, state);
        if (x * x + y * y + z * z <= 1.0f) {
            return Vector3{x * radius, y * radius, z * radius};
        }
    }
    return Vector3{};
}

/** Uniform random point inside a box of the given half-extents. */
Vector3 RandomInBox(const Vector3& halfExtents, std::uint32_t& state) noexcept
{
    return Vector3{
        RandRange(-halfExtents.x, halfExtents.x, state),
        RandRange(-halfExtents.y, halfExtents.y, state),
        RandRange(-halfExtents.z, halfExtents.z, state),
    };
}

/** Uniform random point on a disc of the given radius in the local XZ plane. */
Vector3 RandomOnDisc(float radius, std::uint32_t& state) noexcept
{
    const float a = RandUnit(state) * 2.0f * kPi;
    const float r = radius * std::sqrt(RandUnit(state));
    return Vector3{std::cos(a) * r, 0.0f, std::sin(a) * r};
}

/** Applies only the rotation encoded by a world matrix, removing node scale. */
Vector3 TransformRotationDir(const float m[16], const Vector3& v) noexcept
{
    const float xLength = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float yLength = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float zLength = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    const float inverseX = xLength > 1e-6f ? 1.0f / xLength : 0.0f;
    const float inverseY = yLength > 1e-6f ? 1.0f / yLength : 0.0f;
    const float inverseZ = zLength > 1e-6f ? 1.0f / zLength : 0.0f;
    return Vector3{
        m[0] * inverseX * v.x + m[4] * inverseY * v.y + m[8] * inverseZ * v.z,
        m[1] * inverseX * v.x + m[5] * inverseY * v.y + m[9] * inverseZ * v.z,
        m[2] * inverseX * v.x + m[6] * inverseY * v.y + m[10] * inverseZ * v.z,
    };
}

/** Converts a world direction into the local rotation space of a world matrix. */
Vector3 InverseTransformRotationDir(const float m[16], const Vector3& v) noexcept
{
    const float xLength = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float yLength = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float zLength = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    return Vector3{
        xLength > 1e-6f ? (m[0] * v.x + m[1] * v.y + m[2] * v.z) / xLength : 0.0f,
        yLength > 1e-6f ? (m[4] * v.x + m[5] * v.y + m[6] * v.z) / yLength : 0.0f,
        zLength > 1e-6f ? (m[8] * v.x + m[9] * v.y + m[10] * v.z) / zLength : 0.0f,
    };
}

Vector3 TransformPoint(const float m[16], const Vector3& v) noexcept
{
    return Vector3{
        m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
        m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
        m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14],
    };
}

/**
 * World-space acceleration a single force field exerts on a particle at @p pos.
 * Returns zero outside the field's radius (linear falloff to the edge). Force
 * fields only run in world space; callers gate them on `localSpace == false`.
 */
Vector3 ForceFieldAccel(const ParticleForceField& f, const Vector3& pos) noexcept
{
    const float dx = f.position.x - pos.x;
    const float dy = f.position.y - pos.y;
    const float dz = f.position.z - pos.z;
    const float distSq = dx * dx + dy * dy + dz * dz;
    if (f.radius > 0.0f && distSq > f.radius * f.radius) {
        return Vector3{};
    }
    const float dist = std::sqrt(distSq);
    if (dist < 1e-5f) {
        return Vector3{};
    }
    // Linear falloff from full strength at the anchor to zero at the radius.
    const float falloff = f.radius > 0.0f ? (1.0f - dist / f.radius) : 1.0f;
    const float a = f.strength * falloff;
    const float inv = 1.0f / dist;

    if (f.type == ParticleForceField::Type::Attractor) {
        return Vector3{dx * inv * a, dy * inv * a, dz * inv * a};
    }
    // Positive strength follows the right-hand rule around world +Y.
    const float tx = -dz;
    const float tz = dx;
    const float tlen = std::sqrt(tx * tx + tz * tz);
    if (tlen < 1e-5f) {
        return Vector3{};
    }
    return Vector3{tx / tlen * a, 0.0f, tz / tlen * a};
}

} // namespace

ParticleEmitter::ParticleEmitter(ParticleEmitterDesc desc)
    : m_desc(std::move(desc))
    , m_renderDesc(m_desc)
    // A zero seed would make xorshift32 stall at 0 forever; coerce to 1.
    , m_rngState(m_desc.seed ? m_desc.seed : 1u)
    , m_gpuEmitterKey(NextGpuEmitterKey())
{
    if (m_desc.simulationBackend != ParticleSimulationBackend::Cpu
        && m_desc.simulationBackend != ParticleSimulationBackend::Gpu) {
        m_desc.simulationBackend = ParticleSimulationBackend::Cpu;
    }
    if (m_desc.simulationBackend == ParticleSimulationBackend::Gpu
        && !m_desc.billboard) {
        Debug::Logger::Warn(
            "Particles",
            "GPU simulation currently requires billboard rendering; using CPU simulation");
        m_desc.simulationBackend = ParticleSimulationBackend::Cpu;
    }
    if (m_desc.simulationBackend == ParticleSimulationBackend::Gpu
        && m_desc.capacity > kMaxGpuParticleCapacity) {
        Debug::Logger::Warn(
            "Particles", "GPU particle capacity %u exceeds the %u limit; clamping",
            m_desc.capacity, kMaxGpuParticleCapacity);
        m_desc.capacity = kMaxGpuParticleCapacity;
    }
    m_renderDesc = m_desc;
    m_activeBackend = m_desc.simulationBackend;
    ResolveSimulationBackend();
    if (m_activeBackend == ParticleSimulationBackend::Cpu) {
        m_pool.resize(m_desc.capacity);
    }
    m_renderDesc.bursts.clear();
    m_renderDesc.bursts.shrink_to_fit();
    if (m_desc.simulationBackend == ParticleSimulationBackend::Gpu
        && m_renderDesc.forceFields.size() > kMaxRenderParticleForceFields) {
        Debug::Logger::Warn(
            "Particles", "GPU particle emitter has %zu force fields; using the first %u",
            m_renderDesc.forceFields.size(), kMaxRenderParticleForceFields);
        m_renderDesc.forceFields.resize(kMaxRenderParticleForceFields);
        m_renderDesc.forceFields.shrink_to_fit();
    }
    SetLocalTransform(m_desc.transform);
    // Per-emitter tick: Node dispatches Advance every frame while the scene is
    // active. Advance also handles the initial prewarm on first tick.
    OnUpdate([this](float dt) { Advance(dt); });
}

void ParticleEmitter::Restart()
{
    ResolveSimulationBackend();
    m_elapsed = 0.0f;
    m_emissionAccumulator = 0.0f;
    m_rngState = m_desc.seed ? m_desc.seed : 1u;
    if (m_activeBackend == ParticleSimulationBackend::Gpu) {
        ++m_gpuResetGeneration;
        if (m_gpuResetGeneration == 0) {
            m_gpuResetGeneration = 1;
        }
        m_gpuSpawnBudget.clear();
        m_gpuSpawnSequence = 0;
        m_gpuSimulationTime = 0.0;
        m_gpuFrameSpawnCount = 0;
        m_gpuDeltaTime = 0.0f;
        m_gpuPrewarmSeconds = 0.0f;
    } else {
        for (Particle& p : m_pool) {
            p.alive = false;
        }
    }
    m_alive = 0;
}

void ParticleEmitter::Burst(std::uint32_t count)
{
    ResolveSimulationBackend();
    if (m_activeBackend == ParticleSimulationBackend::Gpu) {
        QueueGpuSpawns(count);
        return;
    }

    const float* w = WorldMatrix();
    for (std::uint32_t i = 0; i < count && m_alive < m_desc.capacity; ++i) {
        SpawnOne(w);
    }
}

void ParticleEmitter::Advance(float deltaTime)
{
    ResolveSimulationBackend();
    if (m_activeBackend == ParticleSimulationBackend::Gpu) {
        AdvanceGpu(deltaTime);
        return;
    }

    if (deltaTime <= 0.0f || m_pool.empty()) {
        return;
    }

    // First tick prewarm: fast-forward `duration` seconds so the emitter starts
    // already populated (no visible spin-up). Bursts do not detonate here.
    if (!m_prewarmed) {
        m_prewarmed = true;
        if (m_desc.prewarm && m_desc.duration > 0.0f) {
            FastForward(m_desc.duration);
        }
    }

    UpdateEmitterVelocity(deltaTime);

    const float prevElapsed = m_elapsed;
    Step(deltaTime, /*allowBursts=*/true);
    FireDueBursts(prevElapsed, m_elapsed, WorldMatrix());

    // Loop wrap: rewind the clock so scheduled bursts re-fire and continuous
    // emission cycles. Live particles are kept — they expire on their own.
    // (prevElapsed is consumed by FireDueBursts above; the wrap is handled here.)
    if (m_desc.loop && m_desc.duration > 0.0f && m_elapsed >= m_desc.duration) {
        m_elapsed = 0.0f;
        m_emissionAccumulator = 0.0f;
        // RNG intentionally not reset: successive loops look different.
    }
}

void ParticleEmitter::FastForward(float seconds)
{
    // Pre-fill the pool by stepping at a fixed sub-step so integration stays
    // stable regardless of the requested duration. Bursts are suppressed so a
    // prewarm does not detonate one-shot explosions.
    constexpr float kMaxStep = 1.0f / 60.0f;
    while (seconds > 0.0f) {
        const float dt = std::min(seconds, kMaxStep);
        Step(dt, /*allowBursts=*/false);
        seconds -= dt;
    }
}

void ParticleEmitter::Step(float dt, bool allowBursts)
{
    m_elapsed += dt;
    Integrate(dt);

    if (m_paused) {
        return;
    }

    // Continuous emission window. When loop=false and duration>0, emission
    // stops once the clock passes duration; when duration==0 and loop=false,
    // there is no continuous stream at all (only bursts). Looping emitters
    // with duration==0 emit forever.
    const bool emitting = m_desc.loop
        ? (m_desc.duration > 0.0f ? m_elapsed < m_desc.duration : true)
        : (m_desc.duration > 0.0f ? m_elapsed < m_desc.duration : false);

    if (emitting) {
        const float* w = WorldMatrix();
        m_emissionAccumulator += m_desc.emissionRate * dt;
        while (m_emissionAccumulator >= 1.0f && m_alive < m_desc.capacity) {
            SpawnOne(w);
            m_emissionAccumulator -= 1.0f;
        }
    }
    // Clamp so a paused-then-resumed emitter does not vomit a stored backlog.
    if (m_emissionAccumulator > 1.0f) {
        m_emissionAccumulator = 1.0f;
    }
}

void ParticleEmitter::Integrate(float dt)
{
    m_alive = 0;
    const float dragFactor = std::max(0.0f, 1.0f - m_desc.drag * dt);
    const float turbPhase = m_elapsed * m_desc.turbulenceFrequency;
    const float turbStrength = m_desc.turbulenceStrength;
    // Force fields are world-space anchors, so they are meaningless (and would
    // double-transform) for a local-space emitter — skip them there.
    const bool useForceFields = !m_desc.localSpace && !m_desc.forceFields.empty();
    const bool hasGround = !m_desc.localSpace && !std::isnan(m_desc.groundY);
    const float maxSp = m_desc.maxSpeed;
    const Vector3 gravity = m_desc.localSpace
        ? InverseTransformRotationDir(WorldMatrix(), m_desc.gravity)
        : m_desc.gravity;

    for (Particle& p : m_pool) {
        if (!p.alive) {
            continue;
        }
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            continue;
        }

        p.velocity.x = p.velocity.x * dragFactor + gravity.x * dt;
        p.velocity.y = p.velocity.y * dragFactor + gravity.y * dt;
        p.velocity.z = p.velocity.z * dragFactor + gravity.z * dt;

        // Localized force fields (attractors / vortices) on top of gravity.
        if (useForceFields) {
            for (const ParticleForceField& f : m_desc.forceFields) {
                const Vector3 a = ForceFieldAccel(f, p.position);
                p.velocity.x += a.x * dt;
                p.velocity.y += a.y * dt;
                p.velocity.z += a.z * dt;
            }
        }

        // Analytic divergence-free curl field for turbulence.
        // The vector field F = (sin(ay+cz), sin(bz+ax), sin(cx+by)) is
        // divergence-free, so its curl gives a swirling velocity field that
        // neither piles particles up nor pushes them out — the look real
        // turbulent flow aims for. Per-particle phase + anisotropic
        // (0.7,1.1,1.7) frequency mix decorrelates each particle.
        if (turbStrength > 0.0f) {
            const float ph = static_cast<float>(p.seed) * 0.137f;
            const float a = 0.7f * m_desc.turbulenceFrequency;
            const float b = 1.1f * m_desc.turbulenceFrequency;
            const float c = 1.7f * m_desc.turbulenceFrequency;
            const float t = turbPhase;
            const float cx_y = std::cos(c * p.position.x + b * p.position.y + ph + t);
            const float bz_x = std::cos(b * p.position.z + a * p.position.x + ph + t);
            const float ay_z = std::cos(a * p.position.y + c * p.position.z + ph + t);
            p.position.x += b * (cx_y - bz_x) * turbStrength * dt;
            p.position.y += c * (ay_z - cx_y) * turbStrength * dt;
            p.position.z += a * (bz_x - ay_z) * turbStrength * dt;
        }

        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;

        // Ground-plane bounce: reflect the downward velocity, dampen the
        // tangential slide by `groundFriction`.
        if (hasGround && p.position.y < m_desc.groundY && p.velocity.y < 0.0f) {
            p.position.y = m_desc.groundY;
            p.velocity.y = -p.velocity.y * m_desc.bounce;
            p.velocity.x *= m_desc.groundFriction;
            p.velocity.z *= m_desc.groundFriction;
        }

        // Hard speed cap, applied after all accelerations this frame.
        if (maxSp > 0.0f) {
            const float sp2 = p.velocity.x * p.velocity.x
                            + p.velocity.y * p.velocity.y
                            + p.velocity.z * p.velocity.z;
            if (sp2 > maxSp * maxSp) {
                const float s = maxSp / std::sqrt(sp2);
                p.velocity.x *= s;
                p.velocity.y *= s;
                p.velocity.z *= s;
            }
        }

        p.rotation.x += p.angularVelocity.x * dt;
        p.rotation.y += p.angularVelocity.y * dt;
        p.rotation.z += p.angularVelocity.z * dt;

        ++m_alive;
    }
}

void ParticleEmitter::SpawnOne(const float* worldMatrix)
{
    for (Particle& p : m_pool) {
        if (p.alive) {
            continue;
        }

        const float spreadRad = std::clamp(m_desc.spreadDegrees, 0.0f, 180.0f) * kDegToRad;
        const Vector3 dirLocal = SampleCone(m_desc.direction, spreadRad, m_rngState);
        const Vector3 dirWorld = TransformRotationDir(worldMatrix, dirLocal);
        const float speed = RandRange(m_desc.speedMin, m_desc.speedMax, m_rngState);

        // Spawn position depends on the emission shape, in emitter-local space.
        Vector3 posLocal{};
        switch (m_desc.shape) {
            case EmitterShape::Point:
                posLocal = Vector3{};
                break;
            case EmitterShape::Sphere:
                posLocal = RandomInBall(m_desc.shapeSize.x, m_rngState);
                break;
            case EmitterShape::Box:
                posLocal = RandomInBox(m_desc.shapeSize, m_rngState);
                break;
            case EmitterShape::Disc:
                posLocal = RandomOnDisc(m_desc.shapeSize.x, m_rngState);
                break;
            case EmitterShape::Cone: {
                const float ang = std::clamp(m_desc.shapeAngleDegrees, 0.0f, 180.0f) * kDegToRad;
                const Vector3 d = SampleCone(m_desc.direction, ang, m_rngState);
                posLocal = Vector3{d.x * m_desc.shapeSize.x, d.y * m_desc.shapeSize.x, d.z * m_desc.shapeSize.x};
                break;
            }
        }

        if (m_desc.localSpace) {
            // Simulate in local space; CollectRender re-transforms by the live
            // matrix so particles follow a moving emitter.
            p.position = posLocal;
            p.velocity = Vector3{dirLocal.x * speed, dirLocal.y * speed, dirLocal.z * speed};
        } else {
            p.position = TransformPoint(worldMatrix, posLocal);
            p.velocity = Vector3{dirWorld.x * speed, dirWorld.y * speed, dirWorld.z * speed};
        }

        // Inject a fraction of the emitter's own velocity so a moving or
        // rotating emitter drags a tail behind it. Applied in the same frame
        // (localSpace: local; world: world) the render path uses.
        if (m_desc.inheritEmitterVelocity > 0.0f) {
            const float k = m_desc.inheritEmitterVelocity;
            const Vector3 emitterSpeed = m_desc.localSpace
                ? InverseTransformRotationDir(worldMatrix, m_emitterSpeed)
                : m_emitterSpeed;
            p.velocity.x += emitterSpeed.x * k;
            p.velocity.y += emitterSpeed.y * k;
            p.velocity.z += emitterSpeed.z * k;
        }

        p.rotation = Vector3{};
        p.angularVelocity = Vector3{
            RandRange(m_desc.rotationVelocityMin.x, m_desc.rotationVelocityMax.x, m_rngState),
            RandRange(m_desc.rotationVelocityMin.y, m_desc.rotationVelocityMax.y, m_rngState),
            RandRange(m_desc.rotationVelocityMin.z, m_desc.rotationVelocityMax.z, m_rngState),
        };
        p.age = 0.0f;
        p.lifetime = std::max(0.05f, RandRange(m_desc.lifetimeMin, m_desc.lifetimeMax, m_rngState));
        p.seed = Xor32(m_rngState); // per-particle turbulence phase
        p.alive = true;
        ++m_alive;
        return;
    }
    // Pool full; drop the request.
}

void ParticleEmitter::FireDueBursts(float previousElapsed, float currentElapsed, const float* worldMatrix)
{
    for (const ParticleBurst& b : m_desc.bursts) {
        // A burst fires when its scheduled time falls in [prev, cur). The
        // half-open lower bound lets a time=0 burst fire on the very first
        // tick (prev=0), while the half-open upper bound avoids re-firing the
        // same instant across two adjacent frames.
        if (b.time >= previousElapsed && b.time < currentElapsed) {
            for (std::uint32_t i = 0; i < b.count && m_alive < m_desc.capacity; ++i) {
                SpawnOne(worldMatrix);
            }
        }
    }
}

void ParticleEmitter::CollectRender(std::vector<RenderInstance>& out) const
{
    if (m_activeBackend == ParticleSimulationBackend::Gpu) {
        return;
    }

    if (m_alive == 0) {
        return;
    }

    const float* w = WorldMatrix();

    // Base material shared by all particles; the per-particle color and size
    // are patched into the copy below. Transparent particles (the additive/
    // alpha default) must not write depth, so overlapping sprites accumulate
    // instead of z-rejecting each other; the backend also orders every blended
    // draw after opaque geometry (see RenderBatcher).
    RenderMaterial base;
    base.lit = !m_desc.billboard && !m_desc.unlit;
    base.metallic = 0.0f;
    base.roughness = 0.9f;
    base.blend = m_desc.blend;
    base.depthWrite = m_desc.blend == Material::BlendMode::Opaque;
    base.cull = m_desc.billboard ? CullMode::None : CullMode::Back;

    for (const Particle& p : m_pool) {
        if (!p.alive) {
            continue;
        }
        const float t = p.lifetime > 0.0f ? (p.age / p.lifetime) : 0.0f;
        const std::uint32_t color = SampleColor3(m_desc.colorStart, m_desc.colorMid, m_desc.colorEnd, t);
        const float size = std::max(0.0f, SampleFloat3(m_desc.sizeStart, m_desc.sizeMid, m_desc.sizeEnd, t));

        RenderInstance instance;
        // Built-in primitives span +/-1 on each axis, so half the size scales
        // them to full extent (matches Object::Box::CollectRender). mtxSRT takes
        // radians; Particle stores Euler degrees.
        // Velocity stretch: billboard VS uses axis lengths for scale.xy, so a
        // longer X axis stretches sparks along their travel (trail/muzzle look).
        const float speed = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y
                                      + p.velocity.z * p.velocity.z);
        const float stretch = m_desc.billboard
            ? (1.0f + std::min(speed * 0.12f, 2.2f))
            : 1.0f;
        const float halfY = size * 0.5f;
        const float halfX = halfY * stretch;
        float srt[16];
        bx::mtxSRT(srt,
                   halfX, halfY, halfY,
                   p.rotation.x * kDegToRad, p.rotation.y * kDegToRad, p.rotation.z * kDegToRad,
                   p.position.x, p.position.y, p.position.z);

        if (m_desc.localSpace) {
            // Compose the local transform with the emitter's live world matrix.
            float composed[16];
            bx::mtxMul(composed, srt, w);
            std::memcpy(instance.world, composed, sizeof(instance.world));
        } else {
            std::memcpy(instance.world, srt, sizeof(instance.world));
        }

        instance.material = base;
        instance.material.albedo = color;
        if (m_desc.billboard || m_desc.unlit) {
            // The unlit branch already outputs albedo once. Add only the energy
            // above 1 here, otherwise brightness=1 doubles the authored colour
            // and turns one additive particle white before overlap or bloom.
            instance.material.emissive = color;
            instance.material.emissiveStrength = std::max(m_desc.brightness - 1.0f, 0.0f);
        }
        instance.shape = m_desc.billboard ? Object::PrimitiveShape::Quad : m_desc.primitiveShape;
        instance.effect = m_desc.billboard ? RenderEffect::ParticleBillboard : RenderEffect::Mesh;
        out.push_back(instance);
    }
}

void ParticleEmitter::CollectParticleEmitters(std::vector<RenderParticleEmitter>& out) const
{
    if (m_activeBackend != ParticleSimulationBackend::Gpu
        || m_desc.capacity == 0) {
        return;
    }

    RenderParticleEmitter emitter;
    emitter.emitterKey = m_gpuEmitterKey;
    emitter.emitterId = Id();
    emitter.resetGeneration = m_gpuResetGeneration;
    emitter.spawnSequence = m_gpuSpawnSequence;
    emitter.spawnCount = m_gpuFrameSpawnCount;
    emitter.simulationTime = m_gpuSimulationTime;
    emitter.deltaTime = m_gpuDeltaTime;
    emitter.prewarmSeconds = m_gpuPrewarmSeconds;
    std::memcpy(emitter.world, WorldMatrix(), sizeof(emitter.world));
    emitter.emitterVelocity = m_emitterSpeed;
    emitter.descriptor = m_renderDesc;
    out.push_back(std::move(emitter));
}

void ParticleEmitter::UpdateEmitterVelocity(float deltaTime)
{
    const float* world = WorldMatrix();
    const Vector3 current{world[12], world[13], world[14]};
    if (m_emitterPosInit && deltaTime > 0.0f) {
        const float inverseDelta = 1.0f / deltaTime;
        m_emitterSpeed = Vector3{
            (current.x - m_lastEmitterPos.x) * inverseDelta,
            (current.y - m_lastEmitterPos.y) * inverseDelta,
            (current.z - m_lastEmitterPos.z) * inverseDelta,
        };
    } else {
        m_emitterSpeed = Vector3{};
        m_emitterPosInit = true;
    }
    m_lastEmitterPos = current;
}

void ParticleEmitter::ResolveSimulationBackend()
{
    if (m_activeBackend != ParticleSimulationBackend::Gpu
        || ParticleSimulationRuntime::GpuAvailability()
            != GpuParticleAvailability::Unavailable) {
        return;
    }

    Debug::Logger::Warn(
        "Particles", "GPU particle simulation is unavailable; using CPU simulation");
    m_activeBackend = ParticleSimulationBackend::Cpu;
    m_pool.assign(m_desc.capacity, Particle{});
    m_gpuSpawnBudget.clear();
    m_gpuSpawnSequence = 0;
    m_gpuFrameSpawnCount = 0;
    m_gpuDeltaTime = 0.0f;
    m_gpuPrewarmSeconds = 0.0f;
    m_alive = 0;
    m_elapsed = 0.0f;
    m_emissionAccumulator = 0.0f;
    m_rngState = m_desc.seed ? m_desc.seed : 1u;
    m_prewarmed = false;
}

void ParticleEmitter::ExpireGpuSpawnBudget()
{
    while (!m_gpuSpawnBudget.empty()
           && m_gpuSpawnBudget.front().expiresAt <= m_gpuSimulationTime) {
        const std::uint32_t expired = m_gpuSpawnBudget.front().count;
        m_alive = expired < m_alive ? m_alive - expired : 0;
        m_gpuSpawnBudget.pop_front();
    }
}

void ParticleEmitter::QueueGpuSpawns(std::uint64_t count)
{
    const std::uint32_t capacity = m_desc.capacity;
    if (capacity == 0 || count == 0) {
        return;
    }

    const std::uint32_t remaining = capacity - std::min(m_alive, capacity);
    const std::uint32_t bounded = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(count, remaining));
    if (bounded == 0) {
        return;
    }
    const std::uint64_t frameCount = static_cast<std::uint64_t>(m_gpuFrameSpawnCount)
        + bounded;
    m_gpuFrameSpawnCount = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(frameCount, capacity));
    if (std::numeric_limits<std::uint64_t>::max() - m_gpuSpawnSequence < bounded) {
        ++m_gpuResetGeneration;
        if (m_gpuResetGeneration == 0) {
            m_gpuResetGeneration = 1;
        }
        m_gpuSpawnSequence = 0;
        m_gpuSpawnBudget.clear();
        m_alive = 0;
    }
    m_gpuSpawnSequence += bounded;

    const float maximumLifetime = std::max(
        0.05f, std::max(m_desc.lifetimeMin, m_desc.lifetimeMax));
    m_gpuSpawnBudget.push_back({m_gpuSimulationTime + maximumLifetime, bounded});
    m_alive = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        capacity, static_cast<std::uint64_t>(m_alive) + bounded));
}

void ParticleEmitter::AdvanceGpu(float deltaTime)
{
    if (deltaTime <= 0.0f || m_desc.capacity == 0) {
        return;
    }

    m_gpuFrameSpawnCount = 0;
    m_gpuDeltaTime = deltaTime;
    m_gpuPrewarmSeconds = 0.0f;
    UpdateEmitterVelocity(deltaTime);
    m_gpuSimulationTime += static_cast<double>(deltaTime);
    ExpireGpuSpawnBudget();

    if (!m_prewarmed) {
        m_prewarmed = true;
        if (m_desc.prewarm && m_desc.duration > 0.0f
            && std::isfinite(m_desc.emissionRate) && m_desc.emissionRate > 0.0f) {
            const float maximumLifetime = std::max(
                0.05f, std::max(m_desc.lifetimeMin, m_desc.lifetimeMax));
            const float history = std::min(m_desc.duration, maximumLifetime);
            const double requested = static_cast<double>(m_desc.emissionRate)
                * static_cast<double>(history);
            QueueGpuSpawns(static_cast<std::uint64_t>(
                std::min<double>(requested, m_desc.capacity)));
            m_gpuPrewarmSeconds = history;
        }
    }

    const float previousElapsed = m_elapsed;
    m_elapsed += deltaTime;
    if (!m_paused) {
        const bool emitting = m_desc.loop
            ? (m_desc.duration > 0.0f ? m_elapsed < m_desc.duration : true)
            : (m_desc.duration > 0.0f ? m_elapsed < m_desc.duration : false);
        if (emitting && std::isfinite(m_desc.emissionRate) && m_desc.emissionRate > 0.0f) {
            m_emissionAccumulator += m_desc.emissionRate * deltaTime;
            const float whole = std::floor(m_emissionAccumulator);
            if (whole > 0.0f) {
                QueueGpuSpawns(static_cast<std::uint64_t>(
                    std::min<double>(whole, m_desc.capacity)));
                m_emissionAccumulator -= whole;
            }
        }

        for (const ParticleBurst& burst : m_desc.bursts) {
            if (burst.time >= previousElapsed && burst.time < m_elapsed) {
                QueueGpuSpawns(burst.count);
            }
        }
    }

    if (m_emissionAccumulator > 1.0f) {
        m_emissionAccumulator = 1.0f;
    }
    if (m_desc.loop && m_desc.duration > 0.0f && m_elapsed >= m_desc.duration) {
        m_elapsed = 0.0f;
        m_emissionAccumulator = 0.0f;
    }
}

} // namespace Concord::Particles
