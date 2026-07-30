#include "engine/scene/io/SceneIOPayload.h"

#include "engine/particles/ParticleEmitter.h"

#include <type_traits>
#include <typeinfo>
#include <utility>

namespace Concord::Detail::SceneIo {

namespace {

void WriteBox(Writer& writer, const Object::Box& box)
{
    WriteTransform(writer, box.LocalTransform());
    writer.PutU8(static_cast<std::uint8_t>(box.Shape()));
    writer.PutVec3(box.Size()); writer.PutU32(box.Color());
    WriteMaterial(writer, box.GetMaterial());
}

Object::BoxDesc ReadBox(Reader& reader)
{
    Object::BoxDesc value;
    value.transform = ReadTransform(reader);
    value.shape = static_cast<Object::PrimitiveShape>(reader.GetU8());
    value.size = reader.GetVec3(); value.color = reader.GetU32();
    value.material = ReadMaterial(reader);
    return value;
}

void WriteLight(Writer& writer, const Object::Light& light)
{
    WriteTransform(writer, light.LocalTransform());
    writer.PutU8(static_cast<std::uint8_t>(light.Type()));
    writer.PutVec3(light.Direction()); writer.PutU32(light.Color());
    writer.PutF32(light.Intensity()); writer.PutF32(light.Range());
    writer.PutF32(light.SourceRadius());
    writer.PutF32(light.DirectionalAngularRadiusDegrees());
    writer.PutF32(light.InnerAngleDegrees()); writer.PutF32(light.OuterAngleDegrees());
    writer.PutU8(light.CastShadow() ? 1u : 0u);
}

Object::LightDesc ReadLight(Reader& reader)
{
    Object::LightDesc value;
    value.transform = ReadTransform(reader);
    value.type = static_cast<LightType>(reader.GetU8());
    value.direction = reader.GetVec3(); value.color = reader.GetU32();
    value.intensity = reader.GetF32(); value.range = reader.GetF32();
    value.sourceRadius = reader.GetF32();
    value.directionalAngularRadiusDegrees = reader.GetF32();
    value.innerAngleDegrees = reader.GetF32(); value.outerAngleDegrees = reader.GetF32();
    value.castShadow = reader.GetU8() != 0;
    return value;
}

void WriteSun(Writer& writer, const Object::SunLight& sun)
{
    WriteTransform(writer, sun.LocalTransform());
    writer.PutF32(sun.LocalSolarTimeHours()); writer.PutF32(sun.LatitudeDegrees());
    writer.PutI32(sun.DayOfYear()); writer.PutF32(sun.NorthYawDegrees());
    writer.PutF32(sun.MaximumIntensity());
    writer.PutF32(sun.DirectionalAngularRadiusDegrees());
    writer.PutU8(sun.CastShadow() ? 1u : 0u);
}

Object::SunLightDesc ReadSun(Reader& reader)
{
    Object::SunLightDesc value;
    value.transform = ReadTransform(reader);
    value.localSolarTimeHours = reader.GetF32(); value.latitudeDegrees = reader.GetF32();
    value.dayOfYear = reader.GetI32(); value.northYawDegrees = reader.GetF32();
    value.maximumIntensity = reader.GetF32();
    value.directionalAngularRadiusDegrees = reader.GetF32();
    value.castShadow = reader.GetU8() != 0;
    return value;
}

void WriteCamera(Writer& writer, const Object::Camera& camera)
{
    WriteTransform(writer, camera.LocalTransform());
    writer.PutF32(camera.Yaw()); writer.PutF32(camera.Pitch());
    writer.PutVec3(camera.UpVector());
    writer.PutU8(static_cast<std::uint8_t>(camera.GetProjection()));
    writer.PutF32(camera.FovYDegrees()); writer.PutF32(camera.OrthoHeight());
    writer.PutF32(camera.NearPlane()); writer.PutF32(camera.FarPlane());
}

ParsedCamera ReadCamera(Reader& reader)
{
    ParsedCamera value;
    value.desc.position = {};
    value.local = ReadTransform(reader); value.yaw = reader.GetF32();
    value.pitch = reader.GetF32(); value.desc.up = reader.GetVec3();
    value.desc.projection = static_cast<Projection>(reader.GetU8());
    value.desc.fovYDegrees = reader.GetF32(); value.desc.orthoHeight = reader.GetF32();
    value.desc.nearPlane = reader.GetF32(); value.desc.farPlane = reader.GetF32();
    return value;
}

void WriteModel(Writer& writer, const Object::Model& model)
{
    const Object::ModelDesc& value = model.Desc();
    WriteTransform(writer, model.LocalTransform()); writer.PutString(value.path);
    WriteMaterial(writer, value.materialOverride);
    writer.PutU8(value.overrideMaterial ? 1u : 0u);
    writer.PutU8(value.autoNormalize ? 1u : 0u);
    writer.PutU8(value.flipWinding ? 1u : 0u);
}

Object::ModelDesc ReadModel(Reader& reader)
{
    Object::ModelDesc value;
    value.transform = ReadTransform(reader); value.path = reader.GetString();
    value.materialOverride = ReadMaterial(reader);
    value.overrideMaterial = reader.GetU8() != 0;
    value.autoNormalize = reader.GetU8() != 0; value.flipWinding = reader.GetU8() != 0;
    return value;
}

void WriteCollider(Writer& writer, const Object::Collider& collider)
{
    WriteTransform(writer, collider.LocalTransform());
    writer.PutCollisionShape(collider.Shape());
    writer.PutU32(collider.Layer()); writer.PutU32(collider.Mask());
}

Object::ColliderDesc ReadCollider(Reader& reader)
{
    Object::ColliderDesc value;
    value.transform = ReadTransform(reader); value.shape = reader.GetCollisionShape();
    value.layer = reader.GetU32(); value.mask = reader.GetU32();
    return value;
}

void WritePivot(Writer& writer, const Object::Node& pivot)
{
    WriteTransform(writer, pivot.LocalTransform());
}

ParsedPivot ReadPivot(Reader& reader)
{
    ParsedPivot value;
    value.local = ReadTransform(reader);
    return value;
}

} // namespace

void WriteNodeSettings(Writer& writer, const Object::Node& node)
{
    writer.PutU8(static_cast<std::uint8_t>(node.GetReflectionMode()));
    writer.PutF32(node.Reflectivity());
}

NodeSettings ReadNodeSettings(Reader& reader)
{
    return {static_cast<Object::ReflectionMode>(reader.GetU8()), reader.GetF32()};
}

bool GetNodeKind(const Object::Node& node, NodeKind& kind)
{
    if (dynamic_cast<const Object::Box*>(&node)) kind = NodeKind::Box;
    else if (dynamic_cast<const Object::SunLight*>(&node)) kind = NodeKind::SunLight;
    else if (dynamic_cast<const Object::Light*>(&node)) kind = NodeKind::Light;
    else if (dynamic_cast<const Object::Camera*>(&node)) kind = NodeKind::Camera;
    else if (dynamic_cast<const Object::Model*>(&node)) kind = NodeKind::Model;
    else if (dynamic_cast<const Object::Collider*>(&node)) kind = NodeKind::Collider;
    else if (dynamic_cast<const Particles::ParticleEmitter*>(&node)) kind = NodeKind::ParticleEmitter;
    // Exact type only: an unknown Node subclass (Character, Sprite, ...) cannot be
    // rebuilt from a transform, so it must stay unsupported rather than degrade
    // into a pivot that silently loses its behaviour.
    else if (typeid(node) == typeid(Object::Node)) kind = NodeKind::Pivot;
    else return false;
    return true;
}

void WriteNodePayload(Writer& writer, NodeKind kind, const Object::Node& node)
{
    WriteNodeSettings(writer, node);
    switch (kind) {
        case NodeKind::Box: WriteBox(writer, static_cast<const Object::Box&>(node)); break;
        case NodeKind::Light: WriteLight(writer, static_cast<const Object::Light&>(node)); break;
        case NodeKind::Camera: WriteCamera(writer, static_cast<const Object::Camera&>(node)); break;
        case NodeKind::Model: WriteModel(writer, static_cast<const Object::Model&>(node)); break;
        case NodeKind::Collider: WriteCollider(writer, static_cast<const Object::Collider&>(node)); break;
        case NodeKind::ParticleEmitter: {
            const auto& emitter = static_cast<const Particles::ParticleEmitter&>(node);
            WriteParticlePayload(writer, emitter.Desc(), emitter.LocalTransform());
            break;
        }
        case NodeKind::SunLight: WriteSun(writer, static_cast<const Object::SunLight&>(node)); break;
        case NodeKind::Pivot: WritePivot(writer, node); break;
    }
}

bool ReadNodePayload(Reader& reader, NodeKind kind, ParsedNode& parsed)
{
    parsed.settings = ReadNodeSettings(reader);
    switch (kind) {
        case NodeKind::Box: parsed.descriptor = ReadBox(reader); break;
        case NodeKind::Light: parsed.descriptor = ReadLight(reader); break;
        case NodeKind::Camera: parsed.descriptor = ReadCamera(reader); break;
        case NodeKind::Model: parsed.descriptor = ReadModel(reader); break;
        case NodeKind::Collider: parsed.descriptor = ReadCollider(reader); break;
        case NodeKind::ParticleEmitter: parsed.descriptor = ReadParticlePayload(reader); break;
        case NodeKind::SunLight: parsed.descriptor = ReadSun(reader); break;
        case NodeKind::Pivot: parsed.descriptor = ReadPivot(reader); break;
        default: reader.Fail(); return false;
    }
    return reader.Ok();
}

std::unique_ptr<Object::Node> CreateNode(ParsedNode parsed)
{
    std::unique_ptr<Object::Node> node = std::visit([](auto&& value) -> std::unique_ptr<Object::Node> {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Object::BoxDesc>) return std::make_unique<Object::Box>(std::move(value));
        else if constexpr (std::is_same_v<T, Object::LightDesc>) return std::make_unique<Object::Light>(std::move(value));
        else if constexpr (std::is_same_v<T, ParsedCamera>) {
            auto camera = std::make_unique<Object::Camera>(std::move(value.desc));
            camera->SetLocalTransform(value.local); camera->SetYaw(value.yaw); camera->SetPitch(value.pitch);
            return camera;
        } else if constexpr (std::is_same_v<T, Object::ModelDesc>) return std::make_unique<Object::Model>(std::move(value));
        else if constexpr (std::is_same_v<T, Object::ColliderDesc>) return std::make_unique<Object::Collider>(std::move(value));
        else if constexpr (std::is_same_v<T, Particles::ParticleEmitterDesc>) return std::make_unique<Particles::ParticleEmitter>(std::move(value));
        else if constexpr (std::is_same_v<T, ParsedPivot>) {
            auto pivot = std::make_unique<Object::Node>();
            pivot->SetLocalTransform(value.local);
            return pivot;
        } else return std::make_unique<Object::SunLight>(std::move(value));
    }, std::move(parsed.descriptor));
    node->SetReflectionMode(parsed.settings.reflectionMode);
    node->SetReflectivity(parsed.settings.reflectivity);
    return node;
}

std::uint32_t ParticleCapacity(const ParsedNode& parsed) noexcept
{
    const auto* emitter = std::get_if<Particles::ParticleEmitterDesc>(&parsed.descriptor);
    return emitter != nullptr ? emitter->capacity : 0;
}

} // namespace Concord::Detail::SceneIo
