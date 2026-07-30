#include "engine/asset/cook/CookedSceneCodec.h"

#include "engine/asset/cook/CookedMath.h"

#include <cmath>

namespace Concord::Asset::Detail::CookedSceneCodec {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::size_t kBurstBytes = 8u;
constexpr std::size_t kForceFieldBytes = 21u;
constexpr std::size_t kFixedPayloadBytes = 178u;

bool Finite(float value) noexcept { return std::isfinite(value); }
bool NonNegative(float value) noexcept { return Finite(value) && value >= 0.0f; }
bool Unit(float value) noexcept
{
    return Finite(value) && value >= 0.0f && value <= 1.0f;
}

bool AddCapacity(std::uint32_t value, const CookedSceneGraphLimits& limits,
                 DecodeBudget& budget) noexcept
{
    if (value == 0u || value > limits.maxParticleCapacity
        || value > limits.maxTotalParticleCapacity - budget.totalParticleCapacity) {
        return false;
    }
    budget.totalParticleCapacity += value;
    return true;
}

} // namespace

bool ValidateParticle(const CookedParticlePayload& value,
                      const CookedSceneGraphLimits& limits,
                      DecodeBudget& budget, std::size_t& encodedBytes)
{
    if (value.shape > Particles::EmitterShape::Cone
        || !IsFinite(value.shapeSize) || value.shapeSize.x < 0.0f
        || value.shapeSize.y < 0.0f || value.shapeSize.z < 0.0f
        || (value.shape != Particles::EmitterShape::Point
            && value.shapeSize.x <= 0.0f)
        || (value.shape == Particles::EmitterShape::Box
            && (value.shapeSize.y <= 0.0f || value.shapeSize.z <= 0.0f))
        || !Finite(value.shapeAngleDegrees)
        || value.shapeAngleDegrees < 0.0f || value.shapeAngleDegrees > 180.0f
        || !NonNegative(value.emissionRate) || !NonNegative(value.duration)
        || !Finite(value.lifetimeMin) || !Finite(value.lifetimeMax)
        || value.lifetimeMin <= 0.0f || value.lifetimeMax < value.lifetimeMin
        || !IsFinite(value.direction)
        || (value.direction.x * value.direction.x
            + value.direction.y * value.direction.y
            + value.direction.z * value.direction.z) <= 1.0e-12f
        || !NonNegative(value.speedMin) || !Finite(value.speedMax)
        || value.speedMax < value.speedMin || !Finite(value.spreadDegrees)
        || value.spreadDegrees < 0.0f || value.spreadDegrees > 180.0f
        || !IsFinite(value.gravity) || !Unit(value.drag)
        || !IsFinite(value.rotationVelocityMin)
        || !IsFinite(value.rotationVelocityMax)
        || !NonNegative(value.turbulenceStrength)
        || !NonNegative(value.turbulenceFrequency)
        || value.forceFields.size() > limits.maxParticleForceFields
        || value.bursts.size() > limits.maxParticleBursts
        || !Finite(value.groundY)
        || !Unit(value.bounce) || !Unit(value.groundFriction)
        || !NonNegative(value.maxSpeed) || !Unit(value.inheritEmitterVelocity)
        || !NonNegative(value.sizeStart) || !NonNegative(value.sizeMid)
        || !NonNegative(value.sizeEnd)
        || value.primitiveShape > Object::PrimitiveShape::Torus
        || value.blend > Material::BlendMode::Additive
        || !NonNegative(value.brightness)
        || value.simulationBackend > Particles::ParticleSimulationBackend::Gpu
        || !AddCapacity(value.capacity, limits, budget)) {
        return false;
    }
    for (const Particles::ParticleForceField& field : value.forceFields) {
        if (field.type > Particles::ParticleForceField::Type::Vortex
            || !IsFinite(field.position) || !Finite(field.strength)
            || !Finite(field.radius)) {
            return false;
        }
    }
    for (const Particles::ParticleBurst& burst : value.bursts) {
        if (!NonNegative(burst.time) || burst.count == 0u) {
            return false;
        }
    }
    encodedBytes += kFixedPayloadBytes
        + value.forceFields.size() * kForceFieldBytes
        + value.bursts.size() * kBurstBytes;
    return true;
}

void WriteParticle(BinaryWriter& writer, const CookedParticlePayload& value)
{
    writer.PutU8(static_cast<std::uint8_t>(value.shape)); PutVec3(writer, value.shapeSize);
    writer.PutF32(value.shapeAngleDegrees); writer.PutF32(value.emissionRate);
    writer.PutF32(value.duration); PutBool(writer, value.loop); PutBool(writer, value.prewarm);
    writer.PutF32(value.lifetimeMin); writer.PutF32(value.lifetimeMax);
    PutVec3(writer, value.direction); writer.PutF32(value.speedMin);
    writer.PutF32(value.speedMax); writer.PutF32(value.spreadDegrees);
    PutVec3(writer, value.gravity); writer.PutF32(value.drag);
    PutVec3(writer, value.rotationVelocityMin); PutVec3(writer, value.rotationVelocityMax);
    writer.PutF32(value.turbulenceStrength); writer.PutF32(value.turbulenceFrequency);
    writer.PutU32(static_cast<std::uint32_t>(value.forceFields.size()));
    for (const Particles::ParticleForceField& field : value.forceFields) {
        writer.PutU8(static_cast<std::uint8_t>(field.type)); PutVec3(writer, field.position);
        writer.PutF32(field.strength); writer.PutF32(field.radius);
    }
    PutBool(writer, value.groundCollision); writer.PutF32(value.groundY);
    writer.PutF32(value.bounce); writer.PutF32(value.groundFriction);
    writer.PutF32(value.maxSpeed); writer.PutF32(value.inheritEmitterVelocity);
    writer.PutU32(value.colorStart); writer.PutU32(value.colorMid); writer.PutU32(value.colorEnd);
    writer.PutF32(value.sizeStart); writer.PutF32(value.sizeMid); writer.PutF32(value.sizeEnd);
    writer.PutU8(static_cast<std::uint8_t>(value.primitiveShape));
    PutBool(writer, value.billboard); writer.PutU32(value.capacity);
    PutBool(writer, value.unlit); writer.PutU8(static_cast<std::uint8_t>(value.blend));
    writer.PutF32(value.brightness); PutBool(writer, value.localSpace);
    writer.PutU8(static_cast<std::uint8_t>(value.simulationBackend)); writer.PutU32(value.seed);
    writer.PutU32(static_cast<std::uint32_t>(value.bursts.size()));
    for (const Particles::ParticleBurst& burst : value.bursts) {
        writer.PutF32(burst.time); writer.PutU32(burst.count);
    }
}

bool ReadParticle(BinaryReader& reader, const CookedSceneGraphLimits& limits,
                  CookedParticlePayload& value)
{
    value.shape = static_cast<Particles::EmitterShape>(reader.GetU8());
    value.shapeSize = GetVec3(reader); value.shapeAngleDegrees = reader.GetF32();
    value.emissionRate = reader.GetF32(); value.duration = reader.GetF32();
    GetBool(reader, value.loop); GetBool(reader, value.prewarm);
    value.lifetimeMin = reader.GetF32(); value.lifetimeMax = reader.GetF32();
    value.direction = GetVec3(reader); value.speedMin = reader.GetF32();
    value.speedMax = reader.GetF32(); value.spreadDegrees = reader.GetF32();
    value.gravity = GetVec3(reader); value.drag = reader.GetF32();
    value.rotationVelocityMin = GetVec3(reader);
    value.rotationVelocityMax = GetVec3(reader);
    value.turbulenceStrength = reader.GetF32(); value.turbulenceFrequency = reader.GetF32();
    const std::uint32_t fieldCount = reader.GetU32();
    if (!reader.Ok() || fieldCount > limits.maxParticleForceFields
        || fieldCount > reader.Remaining() / kForceFieldBytes) return false;
    value.forceFields.resize(fieldCount);
    for (Particles::ParticleForceField& field : value.forceFields) {
        field.type = static_cast<Particles::ParticleForceField::Type>(reader.GetU8());
        field.position = GetVec3(reader); field.strength = reader.GetF32();
        field.radius = reader.GetF32();
    }
    GetBool(reader, value.groundCollision); value.groundY = reader.GetF32();
    value.bounce = reader.GetF32(); value.groundFriction = reader.GetF32();
    value.maxSpeed = reader.GetF32(); value.inheritEmitterVelocity = reader.GetF32();
    value.colorStart = reader.GetU32(); value.colorMid = reader.GetU32();
    value.colorEnd = reader.GetU32(); value.sizeStart = reader.GetF32();
    value.sizeMid = reader.GetF32(); value.sizeEnd = reader.GetF32();
    value.primitiveShape = static_cast<Object::PrimitiveShape>(reader.GetU8());
    GetBool(reader, value.billboard); value.capacity = reader.GetU32();
    GetBool(reader, value.unlit);
    value.blend = static_cast<Material::BlendMode>(reader.GetU8());
    value.brightness = reader.GetF32(); GetBool(reader, value.localSpace);
    value.simulationBackend = static_cast<Particles::ParticleSimulationBackend>(reader.GetU8());
    value.seed = reader.GetU32();
    const std::uint32_t burstCount = reader.GetU32();
    if (!reader.Ok() || burstCount > limits.maxParticleBursts
        || burstCount > reader.Remaining() / kBurstBytes) return false;
    value.bursts.resize(burstCount);
    for (Particles::ParticleBurst& burst : value.bursts) {
        burst.time = reader.GetF32(); burst.count = reader.GetU32();
    }
    return reader.Ok();
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
