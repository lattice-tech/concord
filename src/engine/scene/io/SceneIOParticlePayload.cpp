#include "engine/scene/io/SceneIOPayload.h"

#include "engine/material/BlendMode.h"
#include "engine/particles/ParticleForceField.h"

#include <limits>
#include <stdexcept>

namespace Concord::Detail::SceneIo {

namespace {

constexpr std::size_t kForceFieldBytes = sizeof(std::uint8_t) + sizeof(float) * 5;
constexpr std::size_t kBurstBytes = sizeof(float) + sizeof(std::uint32_t);

}

void ValidateParticleForSave(const Particles::ParticleEmitterDesc& descriptor)
{
    if (descriptor.forceFields.size() > kMaxParticleForceFields
        || descriptor.bursts.size() > kMaxParticleBursts
        || descriptor.capacity > kMaxParticleCapacity) {
        throw std::length_error("particle emitter exceeds the CSCN resource budget");
    }
}

void WriteParticlePayload(Writer& writer, const Particles::ParticleEmitterDesc& value,
                          const Transform& transform)
{
    ValidateParticleForSave(value);
    WriteTransform(writer, transform);
    writer.PutU8(static_cast<std::uint8_t>(value.shape));
    writer.PutVec3(value.shapeSize); writer.PutF32(value.shapeAngleDegrees);
    writer.PutF32(value.emissionRate); writer.PutF32(value.duration);
    writer.PutF32(value.lifetimeMin); writer.PutF32(value.lifetimeMax);
    writer.PutVec3(value.direction); writer.PutF32(value.speedMin); writer.PutF32(value.speedMax);
    writer.PutF32(value.spreadDegrees); writer.PutVec3(value.gravity); writer.PutF32(value.drag);
    writer.PutVec3(value.rotationVelocityMin); writer.PutVec3(value.rotationVelocityMax);
    writer.PutF32(value.turbulenceStrength); writer.PutF32(value.turbulenceFrequency);
    writer.PutU32(static_cast<std::uint32_t>(value.forceFields.size()));
    for (const Particles::ParticleForceField& field : value.forceFields) {
        writer.PutU8(static_cast<std::uint8_t>(field.type)); writer.PutVec3(field.position);
        writer.PutF32(field.strength); writer.PutF32(field.radius);
    }
    writer.PutF32(value.groundY); writer.PutF32(value.bounce);
    writer.PutF32(value.groundFriction); writer.PutF32(value.maxSpeed);
    writer.PutF32(value.inheritEmitterVelocity);
    writer.PutU32(value.colorStart); writer.PutU32(value.colorMid); writer.PutU32(value.colorEnd);
    writer.PutF32(value.sizeStart); writer.PutF32(value.sizeMid); writer.PutF32(value.sizeEnd);
    writer.PutU8(static_cast<std::uint8_t>(value.primitiveShape));
    writer.PutU8(value.billboard ? 1u : 0u); writer.PutU32(value.capacity);
    writer.PutU8(value.unlit ? 1u : 0u); writer.PutU8(static_cast<std::uint8_t>(value.blend));
    writer.PutF32(value.brightness); writer.PutU8(value.localSpace ? 1u : 0u);
    writer.PutU8(static_cast<std::uint8_t>(value.simulationBackend)); writer.PutU32(value.seed);
    writer.PutU32(static_cast<std::uint32_t>(value.bursts.size()));
    for (const Particles::ParticleBurst& burst : value.bursts) {
        writer.PutF32(burst.time); writer.PutU32(burst.count);
    }
}

Particles::ParticleEmitterDesc ReadParticlePayload(Reader& reader)
{
    Particles::ParticleEmitterDesc value;
    value.transform = ReadTransform(reader);
    value.shape = static_cast<Particles::EmitterShape>(reader.GetU8());
    value.shapeSize = reader.GetVec3(); value.shapeAngleDegrees = reader.GetF32();
    value.emissionRate = reader.GetF32(); value.duration = reader.GetF32();
    value.lifetimeMin = reader.GetF32(); value.lifetimeMax = reader.GetF32();
    value.direction = reader.GetVec3(); value.speedMin = reader.GetF32();
    value.speedMax = reader.GetF32(); value.spreadDegrees = reader.GetF32();
    value.gravity = reader.GetVec3(); value.drag = reader.GetF32();
    value.rotationVelocityMin = reader.GetVec3(); value.rotationVelocityMax = reader.GetVec3();
    value.turbulenceStrength = reader.GetF32(); value.turbulenceFrequency = reader.GetF32();

    const std::uint32_t fieldCount = reader.GetU32();
    value.forceFields.clear();
    if (ValidateCount(reader, fieldCount, kMaxParticleForceFields, kForceFieldBytes)) {
        value.forceFields.reserve(fieldCount);
        for (std::uint32_t index = 0; index < fieldCount; ++index) {
            Particles::ParticleForceField field;
            field.type = static_cast<Particles::ParticleForceField::Type>(reader.GetU8());
            field.position = reader.GetVec3(); field.strength = reader.GetF32();
            field.radius = reader.GetF32(); value.forceFields.push_back(field);
        }
    }
    value.groundY = reader.GetF32(); value.bounce = reader.GetF32();
    value.groundFriction = reader.GetF32(); value.maxSpeed = reader.GetF32();
    value.inheritEmitterVelocity = reader.GetF32();
    value.colorStart = reader.GetU32(); value.colorMid = reader.GetU32();
    value.colorEnd = reader.GetU32(); value.sizeStart = reader.GetF32();
    value.sizeMid = reader.GetF32(); value.sizeEnd = reader.GetF32();
    value.primitiveShape = static_cast<Object::PrimitiveShape>(reader.GetU8());
    value.billboard = reader.GetU8() != 0; value.capacity = reader.GetU32();
    if (value.capacity > kMaxParticleCapacity) reader.Fail();
    value.unlit = reader.GetU8() != 0;
    value.blend = static_cast<Material::BlendMode>(reader.GetU8());
    value.brightness = reader.GetF32(); value.localSpace = reader.GetU8() != 0;
    const std::uint8_t backend = reader.GetU8();
    if (backend > static_cast<std::uint8_t>(Particles::ParticleSimulationBackend::Gpu)) {
        reader.Fail();
    } else {
        value.simulationBackend = static_cast<Particles::ParticleSimulationBackend>(backend);
    }
    value.seed = reader.GetU32();
    const std::uint32_t burstCount = reader.GetU32();
    value.bursts.clear();
    if (ValidateCount(reader, burstCount, kMaxParticleBursts, kBurstBytes)) {
        value.bursts.reserve(burstCount);
        for (std::uint32_t index = 0; index < burstCount; ++index) {
            value.bursts.push_back({reader.GetF32(), reader.GetU32()});
        }
    }
    return value;
}

} // namespace Concord::Detail::SceneIo
