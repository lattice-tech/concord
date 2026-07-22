#include "engine/scene/io/SceneIO.h"

#include "engine/collision/CollisionShape.h"
#include "engine/collision/ShapeType.h"
#include "engine/debug/Logger.h"
#include "engine/material/BlendMode.h"
#include "engine/object/Box.h"
#include "engine/object/Camera.h"
#include "engine/object/Collider.h"
#include "engine/object/Light.h"
#include "engine/object/light/SunLight.h"
#include "engine/object/Model.h"
#include "engine/object/Node.h"
#include "engine/particles/ParticleEmitter.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "engine/particles/ParticleForceField.h"
#include "engine/scene/Scene.h"

#include <bit>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Concord {

namespace {

constexpr std::uint32_t kMagic = 0x4E435343u; // 'CSCN' in file order
constexpr std::uint32_t kVersion = 6;
constexpr std::size_t kMaxSceneFileBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kMaxSceneNodes = 65'536u;
constexpr std::uint32_t kMaxStringBytes = 1024u * 1024u;
constexpr std::uint32_t kMaxParticleForceFields = 1024u;
constexpr std::uint32_t kMaxParticleBursts = 16'384u;
constexpr std::uint32_t kMaxParticleCapacity = 65'536u;
constexpr std::uint32_t kMaxTotalParticleCapacity = 262'144u;
constexpr std::size_t kNodePrefixBytes = sizeof(std::uint8_t) * 2 + sizeof(float);
constexpr std::size_t kForceFieldBytes = sizeof(std::uint8_t) + sizeof(float) * 5;
constexpr std::size_t kBurstBytes = sizeof(float) + sizeof(std::uint32_t);

/** Identifies the descriptor encoded after each common node header. */
enum class NodeKind : std::uint8_t {
    Box = 0,
    Light = 1,
    Camera = 2,
    Model = 3,
    Collider = 4,
    ParticleEmitter = 5,
    SunLight = 6,
};

/** Writes the little-endian CSCN representation into contiguous storage. */
class Writer {
public:
    void PutU8(std::uint8_t v) { buf.push_back(v); }
    void PutU32(std::uint32_t v)
    {
        buf.push_back(static_cast<std::uint8_t>(v));
        buf.push_back(static_cast<std::uint8_t>(v >> 8u));
        buf.push_back(static_cast<std::uint8_t>(v >> 16u));
        buf.push_back(static_cast<std::uint8_t>(v >> 24u));
    }
    void PutI32(std::int32_t v) { PutU32(std::bit_cast<std::uint32_t>(v)); }
    void PutF32(float v) { PutU32(std::bit_cast<std::uint32_t>(v)); }
    void PatchU32(std::size_t offset, std::uint32_t v)
    {
        if (offset > buf.size() || sizeof(v) > buf.size() - offset) {
            throw std::out_of_range("scene output patch is outside the buffer");
        }
        buf[offset] = static_cast<std::uint8_t>(v);
        buf[offset + 1u] = static_cast<std::uint8_t>(v >> 8u);
        buf[offset + 2u] = static_cast<std::uint8_t>(v >> 16u);
        buf[offset + 3u] = static_cast<std::uint8_t>(v >> 24u);
    }
    void PutVec3(const Vector3& v)     { PutF32(v.x); PutF32(v.y); PutF32(v.z); }
    void PutQuat(const Quaternion& q)  { PutF32(q.x); PutF32(q.y); PutF32(q.z); PutF32(q.w); }
    void PutString(const std::string& s)
    {
        if (s.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("scene string exceeds the CSCN length field");
        }
        PutU32(static_cast<std::uint32_t>(s.size()));
        buf.insert(buf.end(), s.data(), s.data() + s.size());
    }
    void PutCollisionShape(const Collision::CollisionShape& s)
    {
        PutU8(static_cast<std::uint8_t>(s.type));
        PutVec3(s.halfExtents);
        PutF32(s.radius);
        PutVec3(s.offset);
    }

    void Reserve(std::size_t additional)
    {
        if (additional > buf.max_size() - buf.size()) {
            throw std::length_error("scene output exceeds vector capacity");
        }
        buf.reserve(buf.size() + additional);
    }

    std::vector<std::uint8_t> buf;
};

void WriteTransform(Writer& w, const Transform& t)
{
    w.PutVec3(t.position);
    w.PutQuat(t.rotation);
    w.PutVec3(t.scale);
}

void WriteSkyEnvironment(Writer& w, const SkyEnvironment& environment)
{
    w.PutU8(static_cast<std::uint8_t>(environment.mode));
    w.PutU32(environment.solidColor);
    w.PutU32(environment.zenithColor);
    w.PutU32(environment.horizonColor);
    w.PutU32(environment.groundColor);
    w.PutU32(environment.ambientColor);
    w.PutF32(environment.intensity);
    w.PutF32(environment.ambientIntensity);
    w.PutF32(environment.nightAmbientIntensity);
    w.PutF32(environment.horizonFalloff);
    w.PutF32(environment.sunDiskIntensity);
    w.PutU8(environment.sunDisk ? 1u : 0u);
}

void WriteNodeSettings(Writer& w, const Object::Node& node)
{
    w.PutU8(static_cast<std::uint8_t>(node.GetReflectionMode()));
    w.PutF32(node.Reflectivity());
}

void WriteMaterial(Writer& w, const Material::MaterialDesc& m)
{
    w.PutU8(static_cast<std::uint8_t>(m.model));

    w.PutU32(m.surface.albedo);
    w.PutF32(m.surface.metallic);
    w.PutF32(m.surface.roughness);
    w.PutF32(m.surface.reflectivity);
    w.PutU32(m.surface.emissive);
    w.PutF32(m.surface.emissiveStrength);

    w.PutU8(m.gradient.enabled ? 1u : 0u);
    w.PutU32(m.gradient.from);
    w.PutU32(m.gradient.to);
    w.PutU8(static_cast<std::uint8_t>(m.gradient.axis));

    w.PutString(m.textures.albedo.path);
    w.PutString(m.textures.normal.path);
    w.PutString(m.textures.metallicRoughness.path);
    w.PutString(m.textures.emissive.path);

    w.PutU8(static_cast<std::uint8_t>(m.draw.depthTest));
    w.PutU8(m.draw.depthWrite ? 1u : 0u);
    w.PutU8(static_cast<std::uint8_t>(m.draw.cull));
    w.PutU8(static_cast<std::uint8_t>(m.draw.blend));
    w.PutI32(m.draw.priority);
    w.PutU8(m.planarReflection ? 1u : 0u);
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size), m_pos(0) {}

    bool Ok() const noexcept { return !m_error; }
    bool AtEnd() const noexcept { return !m_error && m_pos == m_size; }
    std::size_t Remaining() const noexcept
    {
        return m_pos <= m_size ? m_size - m_pos : 0;
    }
    void Fail() noexcept { m_error = true; }

    std::uint8_t GetU8()
    {
        if (!CanRead(1)) { m_error = true; return 0; }
        return m_data[m_pos++];
    }
    std::uint32_t GetU32()
    {
        if (!CanRead(sizeof(std::uint32_t))) { m_error = true; return 0; }
        const std::uint32_t v = static_cast<std::uint32_t>(m_data[m_pos])
            | static_cast<std::uint32_t>(m_data[m_pos + 1u]) << 8u
            | static_cast<std::uint32_t>(m_data[m_pos + 2u]) << 16u
            | static_cast<std::uint32_t>(m_data[m_pos + 3u]) << 24u;
        m_pos += sizeof(v);
        return v;
    }
    std::int32_t GetI32() { return std::bit_cast<std::int32_t>(GetU32()); }
    float GetF32() { return std::bit_cast<float>(GetU32()); }
    Vector3 GetVec3()   { Vector3 v; v.x = GetF32(); v.y = GetF32(); v.z = GetF32(); return v; }
    Quaternion GetQuat(){ Quaternion q; q.x = GetF32(); q.y = GetF32(); q.z = GetF32(); q.w = GetF32(); return q; }
    std::string GetString()
    {
        const std::uint32_t n = GetU32();
        if (n > kMaxStringBytes || !CanRead(n)) { m_error = true; return {}; }
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), n);
        m_pos += n;
        return s;
    }
    Collision::CollisionShape GetCollisionShape()
    {
        Collision::CollisionShape s;
        s.type = static_cast<Collision::ShapeType>(GetU8());
        s.halfExtents = GetVec3();
        s.radius = GetF32();
        s.offset = GetVec3();
        return s;
    }

private:
    bool CanRead(std::size_t count) const noexcept
    {
        return !m_error && m_pos <= m_size && count <= m_size - m_pos;
    }

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos;
    bool m_error = false;
};

Transform ReadTransform(Reader& r)
{
    Transform t;
    t.position = r.GetVec3();
    t.rotation = r.GetQuat();
    t.scale = r.GetVec3();
    return t;
}

SkyEnvironment ReadSkyEnvironment(Reader& r)
{
    SkyEnvironment environment;
    environment.mode = static_cast<SkyMode>(r.GetU8());
    environment.solidColor = r.GetU32();
    environment.zenithColor = r.GetU32();
    environment.horizonColor = r.GetU32();
    environment.groundColor = r.GetU32();
    environment.ambientColor = r.GetU32();
    environment.intensity = r.GetF32();
    environment.ambientIntensity = r.GetF32();
    environment.nightAmbientIntensity = r.GetF32();
    environment.horizonFalloff = r.GetF32();
    environment.sunDiskIntensity = r.GetF32();
    environment.sunDisk = r.GetU8() != 0;
    return environment;
}

struct NodeSettings {
    Object::ReflectionMode reflectionMode = Object::ReflectionMode::Standard;
    float reflectivity = 1.0f;
};

NodeSettings ReadNodeSettings(Reader& r)
{
    NodeSettings settings;
    settings.reflectionMode = static_cast<Object::ReflectionMode>(r.GetU8());
    settings.reflectivity = r.GetF32();
    return settings;
}

void ApplyNodeSettings(Object::Node& node, const NodeSettings& settings)
{
    node.SetReflectionMode(settings.reflectionMode);
    node.SetReflectivity(settings.reflectivity);
}

struct ParsedCamera {
    Object::CameraDesc desc;
    Transform local;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

using NodeDescriptor = std::variant<
    Object::BoxDesc,
    Object::LightDesc,
    ParsedCamera,
    Object::ModelDesc,
    Object::ColliderDesc,
    Particles::ParticleEmitterDesc,
    Object::SunLightDesc>;

struct ParsedNode {
    NodeSettings settings;
    NodeDescriptor descriptor;
};

bool ValidateCount(Reader& reader, std::uint32_t count, std::uint32_t maximum,
                   std::size_t minimumElementBytes)
{
    if (!reader.Ok() || count > maximum
        || count > reader.Remaining() / minimumElementBytes) {
        reader.Fail();
        return false;
    }
    return true;
}

std::unique_ptr<Object::Node> CreateNode(ParsedNode parsed)
{
    std::unique_ptr<Object::Node> node = std::visit(
        [](auto&& descriptor) -> std::unique_ptr<Object::Node> {
            using Descriptor = std::remove_cvref_t<decltype(descriptor)>;
            if constexpr (std::is_same_v<Descriptor, Object::BoxDesc>) {
                return std::make_unique<Object::Box>(std::move(descriptor));
            } else if constexpr (std::is_same_v<Descriptor, Object::LightDesc>) {
                return std::make_unique<Object::Light>(std::move(descriptor));
            } else if constexpr (std::is_same_v<Descriptor, ParsedCamera>) {
                auto camera = std::make_unique<Object::Camera>(std::move(descriptor.desc));
                camera->SetLocalTransform(descriptor.local);
                camera->SetYaw(descriptor.yaw);
                camera->SetPitch(descriptor.pitch);
                return camera;
            } else if constexpr (std::is_same_v<Descriptor, Object::ModelDesc>) {
                return std::make_unique<Object::Model>(std::move(descriptor));
            } else if constexpr (std::is_same_v<Descriptor, Object::ColliderDesc>) {
                return std::make_unique<Object::Collider>(std::move(descriptor));
            } else if constexpr (std::is_same_v<Descriptor, Particles::ParticleEmitterDesc>) {
                return std::make_unique<Particles::ParticleEmitter>(std::move(descriptor));
            } else {
                static_assert(std::is_same_v<Descriptor, Object::SunLightDesc>);
                return std::make_unique<Object::SunLight>(std::move(descriptor));
            }
        },
        std::move(parsed.descriptor));
    ApplyNodeSettings(*node, parsed.settings);
    return node;
}

Material::MaterialDesc ReadMaterial(Reader& r)
{
    Material::MaterialDesc m;
    m.model = static_cast<Material::MaterialModel>(r.GetU8());

    m.surface.albedo = r.GetU32();
    m.surface.metallic = r.GetF32();
    m.surface.roughness = r.GetF32();
    m.surface.reflectivity = r.GetF32();
    m.surface.emissive = r.GetU32();
    m.surface.emissiveStrength = r.GetF32();

    m.gradient.enabled = r.GetU8() != 0;
    m.gradient.from = r.GetU32();
    m.gradient.to = r.GetU32();
    m.gradient.axis = static_cast<Material::GradientAxis>(r.GetU8());

    m.textures.albedo.path = r.GetString();
    m.textures.normal.path = r.GetString();
    m.textures.metallicRoughness.path = r.GetString();
    m.textures.emissive.path = r.GetString();

    m.draw.depthTest = static_cast<DepthTest>(r.GetU8());
    m.draw.depthWrite = r.GetU8() != 0;
    m.draw.cull = static_cast<CullMode>(r.GetU8());
    m.draw.blend = static_cast<Material::BlendMode>(r.GetU8());
    m.draw.priority = r.GetI32();
    m.planarReflection = r.GetU8() != 0;
    return m;
}

void WriteBox(Writer& w, const Object::Box& box)
{
    WriteTransform(w, box.LocalTransform());
    w.PutU8(static_cast<std::uint8_t>(box.Shape()));
    w.PutVec3(box.Size());
    w.PutU32(box.Color());
    WriteMaterial(w, box.GetMaterial());
}

Object::BoxDesc ReadBox(Reader& r)
{
    Object::BoxDesc desc;
    desc.transform = ReadTransform(r);
    desc.shape = static_cast<Object::PrimitiveShape>(r.GetU8());
    desc.size = r.GetVec3();
    desc.color = r.GetU32();
    desc.material = ReadMaterial(r);
    return desc;
}

void WriteLight(Writer& w, const Object::Light& light)
{
    WriteTransform(w, light.LocalTransform());
    w.PutU8(static_cast<std::uint8_t>(light.Type()));
    w.PutVec3(light.Direction());
    w.PutU32(light.Color());
    w.PutF32(light.Intensity());
    w.PutF32(light.Range());
    w.PutF32(light.SourceRadius());
    w.PutF32(light.DirectionalAngularRadiusDegrees());
    w.PutF32(light.InnerAngleDegrees());
    w.PutF32(light.OuterAngleDegrees());
    w.PutU8(light.CastShadow() ? 1u : 0u);
}

Object::LightDesc ReadLight(Reader& r)
{
    Object::LightDesc desc;
    desc.transform = ReadTransform(r);
    desc.type = static_cast<LightType>(r.GetU8());
    desc.direction = r.GetVec3();
    desc.color = r.GetU32();
    desc.intensity = r.GetF32();
    desc.range = r.GetF32();
    desc.sourceRadius = r.GetF32();
    desc.directionalAngularRadiusDegrees = r.GetF32();
    desc.innerAngleDegrees = r.GetF32();
    desc.outerAngleDegrees = r.GetF32();
    desc.castShadow = r.GetU8() != 0;
    return desc;
}

void WriteSunLight(Writer& w, const Object::SunLight& sun)
{
    WriteTransform(w, sun.LocalTransform());
    w.PutF32(sun.LocalSolarTimeHours());
    w.PutF32(sun.LatitudeDegrees());
    w.PutI32(sun.DayOfYear());
    w.PutF32(sun.NorthYawDegrees());
    w.PutF32(sun.MaximumIntensity());
    w.PutF32(sun.DirectionalAngularRadiusDegrees());
    w.PutU8(sun.CastShadow() ? 1u : 0u);
}

Object::SunLightDesc ReadSunLight(Reader& r)
{
    Object::SunLightDesc desc;
    desc.transform = ReadTransform(r);
    desc.localSolarTimeHours = r.GetF32();
    desc.latitudeDegrees = r.GetF32();
    desc.dayOfYear = r.GetI32();
    desc.northYawDegrees = r.GetF32();
    desc.maximumIntensity = r.GetF32();
    desc.directionalAngularRadiusDegrees = r.GetF32();
    desc.castShadow = r.GetU8() != 0;
    return desc;
}

void WriteCamera(Writer& w, const Object::Camera& cam)
{
    WriteTransform(w, cam.LocalTransform());
    w.PutF32(cam.Yaw());
    w.PutF32(cam.Pitch());
    w.PutVec3(cam.UpVector());
    w.PutU8(static_cast<std::uint8_t>(cam.GetProjection()));
    w.PutF32(cam.FovYDegrees());
    w.PutF32(cam.OrthoHeight());
    w.PutF32(cam.NearPlane());
    w.PutF32(cam.FarPlane());
}

ParsedCamera ReadCamera(Reader& r)
{
    ParsedCamera camera;
    camera.desc.position = Vector3{};
    camera.local = ReadTransform(r);
    camera.yaw = r.GetF32();
    camera.pitch = r.GetF32();
    camera.desc.up = r.GetVec3();
    camera.desc.projection = static_cast<Projection>(r.GetU8());
    camera.desc.fovYDegrees = r.GetF32();
    camera.desc.orthoHeight = r.GetF32();
    camera.desc.nearPlane = r.GetF32();
    camera.desc.farPlane = r.GetF32();
    return camera;
}

void WriteModel(Writer& w, const Object::Model& model)
{
    const Object::ModelDesc& d = model.Desc();
    WriteTransform(w, model.LocalTransform());
    w.PutString(d.path);
    WriteMaterial(w, d.materialOverride);
    w.PutU8(d.overrideMaterial ? 1u : 0u);
    w.PutU8(d.autoNormalize ? 1u : 0u);
    w.PutU8(d.flipWinding ? 1u : 0u);
}

Object::ModelDesc ReadModel(Reader& r)
{
    Object::ModelDesc desc;
    desc.transform = ReadTransform(r);
    desc.path = r.GetString();
    desc.materialOverride = ReadMaterial(r);
    desc.overrideMaterial = r.GetU8() != 0;
    desc.autoNormalize = r.GetU8() != 0;
    desc.flipWinding = r.GetU8() != 0;
    return desc;
}

void WriteCollider(Writer& w, const Object::Collider& c)
{
    WriteTransform(w, c.LocalTransform());
    w.PutCollisionShape(c.Shape());
    w.PutU32(c.Layer());
    w.PutU32(c.Mask());
}

Object::ColliderDesc ReadCollider(Reader& r)
{
    Object::ColliderDesc desc;
    desc.transform = ReadTransform(r);
    desc.shape = r.GetCollisionShape();
    desc.layer = r.GetU32();
    desc.mask = r.GetU32();
    return desc;
}

void WriteParticleEmitter(Writer& w, const Particles::ParticleEmitter& e)
{
    const Particles::ParticleEmitterDesc& d = e.Desc();
    if (d.forceFields.size() > std::numeric_limits<std::uint32_t>::max()
        || d.bursts.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("particle emitter list exceeds the CSCN count field");
    }
    WriteTransform(w, e.LocalTransform());

    w.PutU8(static_cast<std::uint8_t>(d.shape));
    w.PutVec3(d.shapeSize);
    w.PutF32(d.shapeAngleDegrees);

    w.PutF32(d.emissionRate);
    w.PutF32(d.duration);
    w.PutF32(d.lifetimeMin);
    w.PutF32(d.lifetimeMax);

    w.PutVec3(d.direction);
    w.PutF32(d.speedMin);
    w.PutF32(d.speedMax);
    w.PutF32(d.spreadDegrees);
    w.PutVec3(d.gravity);
    w.PutF32(d.drag);
    w.PutVec3(d.rotationVelocityMin);
    w.PutVec3(d.rotationVelocityMax);
    w.PutF32(d.turbulenceStrength);
    w.PutF32(d.turbulenceFrequency);

    w.PutU32(static_cast<std::uint32_t>(d.forceFields.size()));
    for (const auto& f : d.forceFields) {
        w.PutU8(static_cast<std::uint8_t>(f.type));
        w.PutVec3(f.position);
        w.PutF32(f.strength);
        w.PutF32(f.radius);
    }

    // Ground-plane collision + speed cap. groundY is a raw IEEE-754 f32 so a
    // saved NaN round-trips as NaN and continues to disable the bounce.
    w.PutF32(d.groundY);
    w.PutF32(d.bounce);
    w.PutF32(d.groundFriction);
    w.PutF32(d.maxSpeed);
    w.PutF32(d.inheritEmitterVelocity);

    w.PutU32(d.colorStart);
    w.PutU32(d.colorMid);
    w.PutU32(d.colorEnd);
    w.PutF32(d.sizeStart);
    w.PutF32(d.sizeMid);
    w.PutF32(d.sizeEnd);
    w.PutU8(static_cast<std::uint8_t>(d.primitiveShape));
    w.PutU8(d.billboard ? 1u : 0u);
    w.PutU32(d.capacity);
    w.PutU8(d.unlit ? 1u : 0u);
    w.PutU8(static_cast<std::uint8_t>(d.blend));
    w.PutF32(d.brightness);
    w.PutU8(d.localSpace ? 1u : 0u);
    w.PutU8(static_cast<std::uint8_t>(d.simulationBackend));
    w.PutU32(d.seed);

    w.PutU32(static_cast<std::uint32_t>(d.bursts.size()));
    for (const auto& b : d.bursts) {
        w.PutF32(b.time);
        w.PutU32(b.count);
    }
}

Particles::ParticleEmitterDesc ReadParticleEmitter(Reader& r)
{
    Particles::ParticleEmitterDesc d;
    d.transform = ReadTransform(r);

    d.shape = static_cast<Particles::EmitterShape>(r.GetU8());
    d.shapeSize = r.GetVec3();
    d.shapeAngleDegrees = r.GetF32();

    d.emissionRate = r.GetF32();
    d.duration = r.GetF32();
    d.lifetimeMin = r.GetF32();
    d.lifetimeMax = r.GetF32();

    d.direction = r.GetVec3();
    d.speedMin = r.GetF32();
    d.speedMax = r.GetF32();
    d.spreadDegrees = r.GetF32();
    d.gravity = r.GetVec3();
    d.drag = r.GetF32();
    d.rotationVelocityMin = r.GetVec3();
    d.rotationVelocityMax = r.GetVec3();
    d.turbulenceStrength = r.GetF32();
    d.turbulenceFrequency = r.GetF32();

    const std::uint32_t fieldCount = r.GetU32();
    d.forceFields.clear();
    if (ValidateCount(r, fieldCount, kMaxParticleForceFields, kForceFieldBytes)) {
        d.forceFields.reserve(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            Particles::ParticleForceField f;
            f.type = static_cast<Particles::ParticleForceField::Type>(r.GetU8());
            f.position = r.GetVec3();
            f.strength = r.GetF32();
            f.radius = r.GetF32();
            d.forceFields.push_back(f);
        }
    }

    d.groundY = r.GetF32();
    d.bounce = r.GetF32();
    d.groundFriction = r.GetF32();
    d.maxSpeed = r.GetF32();
    d.inheritEmitterVelocity = r.GetF32();

    d.colorStart = r.GetU32();
    d.colorMid = r.GetU32();
    d.colorEnd = r.GetU32();
    d.sizeStart = r.GetF32();
    d.sizeMid = r.GetF32();
    d.sizeEnd = r.GetF32();
    d.primitiveShape = static_cast<Object::PrimitiveShape>(r.GetU8());
    d.billboard = r.GetU8() != 0;
    d.capacity = r.GetU32();
    if (d.capacity > kMaxParticleCapacity) {
        r.Fail();
    }
    d.unlit = r.GetU8() != 0;
    d.blend = static_cast<Material::BlendMode>(r.GetU8());
    d.brightness = r.GetF32();
    d.localSpace = r.GetU8() != 0;
    const std::uint8_t simulationBackend = r.GetU8();
    if (simulationBackend > static_cast<std::uint8_t>(
            Particles::ParticleSimulationBackend::Gpu)) {
        r.Fail();
    } else {
        d.simulationBackend = static_cast<Particles::ParticleSimulationBackend>(
            simulationBackend);
    }
    d.seed = r.GetU32();

    const std::uint32_t burstCount = r.GetU32();
    d.bursts.clear();
    if (ValidateCount(r, burstCount, kMaxParticleBursts, kBurstBytes)) {
        d.bursts.reserve(burstCount);
        for (std::uint32_t i = 0; i < burstCount; ++i) {
            Particles::ParticleBurst b;
            b.time = r.GetF32();
            b.count = r.GetU32();
            d.bursts.push_back(b);
        }
    }

    return d;
}

} // namespace

bool SceneIO::Save(const Scene& scene, const std::string& path)
{
    try {
        Writer w;
        w.PutU32(kMagic);
        w.PutU32(kVersion);
        WriteSkyEnvironment(w, scene.GetSkyEnvironment());

        const std::vector<Object::Node*> nodes = scene.Nodes();
        std::uint32_t written = 0;
        const std::size_t countPos = w.buf.size();
        w.PutU32(0);

        constexpr std::size_t kInitialBytesPerNode = 384u;
        if (nodes.size() <= (std::numeric_limits<std::size_t>::max() - 16u)
                / kInitialBytesPerNode) {
            w.Reserve(nodes.size() * kInitialBytesPerNode + 16u);
        }

        for (Object::Node* n : nodes) {
            if (auto* box = dynamic_cast<Object::Box*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::Box));
                WriteNodeSettings(w, *box);
                WriteBox(w, *box);
                ++written;
            } else if (auto* sun = dynamic_cast<Object::SunLight*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::SunLight));
                WriteNodeSettings(w, *sun);
                WriteSunLight(w, *sun);
                ++written;
            } else if (auto* light = dynamic_cast<Object::Light*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::Light));
                WriteNodeSettings(w, *light);
                WriteLight(w, *light);
                ++written;
            } else if (auto* cam = dynamic_cast<Object::Camera*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::Camera));
                WriteNodeSettings(w, *cam);
                WriteCamera(w, *cam);
                ++written;
            } else if (auto* model = dynamic_cast<Object::Model*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::Model));
                WriteNodeSettings(w, *model);
                WriteModel(w, *model);
                ++written;
            } else if (auto* collider = dynamic_cast<Object::Collider*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::Collider));
                WriteNodeSettings(w, *collider);
                WriteCollider(w, *collider);
                ++written;
            } else if (auto* particles = dynamic_cast<Particles::ParticleEmitter*>(n)) {
                w.PutU8(static_cast<std::uint8_t>(NodeKind::ParticleEmitter));
                WriteNodeSettings(w, *particles);
                WriteParticleEmitter(w, *particles);
                ++written;
            }
        }
        w.PatchU32(countPos, written);

        std::error_code dirError;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent, dirError)) {
            std::filesystem::create_directories(parent, dirError);
            if (dirError) {
                Debug::Logger::Error("Scene", "cscene save: cannot create directory '%s' (%s)",
                                     parent.string().c_str(), dirError.message().c_str());
                return false;
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            Debug::Logger::Error("Scene", "cscene save: cannot open '%s' for writing", path.c_str());
            return false;
        }
        out.write(reinterpret_cast<const char*>(w.buf.data()),
                  static_cast<std::streamsize>(w.buf.size()));
        if (!out) {
            Debug::Logger::Error("Scene", "cscene save: write error on '%s'", path.c_str());
            return false;
        }
        Debug::Logger::Info("Scene", "saved '%s' (%u objects, %zu bytes)",
                            path.c_str(), written, w.buf.size());
        return true;
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Scene", "cscene save: '%s' failed (%s)",
                             path.c_str(), exception.what());
        return false;
    } catch (...) {
        Debug::Logger::Error("Scene", "cscene save: '%s' failed", path.c_str());
        return false;
    }
}

SceneLoadResult SceneIO::Load(Scene& scene, const std::string& path)
{
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            Debug::Logger::Error("Scene", "cscene load: cannot open '%s'", path.c_str());
            return {};
        }
        const std::streamsize size = in.tellg();
        if (size <= 0) {
            Debug::Logger::Error("Scene", "cscene load: empty or unreadable '%s'", path.c_str());
            return {};
        }
        if (size > static_cast<std::streamsize>(kMaxSceneFileBytes)) {
            Debug::Logger::Error("Scene", "cscene load: '%s' exceeds the %zu-byte limit",
                                 path.c_str(), kMaxSceneFileBytes);
            return {};
        }
        in.seekg(0);
        std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
        if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
            Debug::Logger::Error("Scene", "cscene load: read error on '%s'", path.c_str());
            return {};
        }

        Reader r(data.data(), data.size());
        const std::uint32_t magic = r.GetU32();
        const std::uint32_t version = r.GetU32();
        if (magic != kMagic) {
            Debug::Logger::Error("Scene", "cscene load: bad magic in '%s' (0x%08x != 0x%08x)",
                                 path.c_str(), magic, kMagic);
            return {};
        }
        if (version != kVersion) {
            Debug::Logger::Error("Scene", "cscene load: version %u in '%s' (expected %u)",
                                 version, path.c_str(), kVersion);
            return {};
        }

        const SkyEnvironment environment = ReadSkyEnvironment(r);
        if (!r.Ok()) {
            Debug::Logger::Error("Scene", "cscene load: truncated sky in '%s'", path.c_str());
            return {};
        }

        const std::uint32_t count = r.GetU32();
        if (!ValidateCount(r, count, kMaxSceneNodes, kNodePrefixBytes)) {
            Debug::Logger::Error("Scene", "cscene load: invalid object count %u in '%s'",
                                 count, path.c_str());
            return {};
        }

        std::vector<ParsedNode> parsedNodes;
        parsedNodes.reserve(count);
        std::uint32_t totalParticleCapacity = 0;
        for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
            const auto kind = static_cast<NodeKind>(r.GetU8());
            ParsedNode parsed;
            parsed.settings = ReadNodeSettings(r);
            switch (kind) {
                case NodeKind::Box:             parsed.descriptor = ReadBox(r); break;
                case NodeKind::Light:           parsed.descriptor = ReadLight(r); break;
                case NodeKind::Camera:          parsed.descriptor = ReadCamera(r); break;
                case NodeKind::Model:           parsed.descriptor = ReadModel(r); break;
                case NodeKind::Collider:        parsed.descriptor = ReadCollider(r); break;
                case NodeKind::ParticleEmitter: parsed.descriptor = ReadParticleEmitter(r); break;
                case NodeKind::SunLight:        parsed.descriptor = ReadSunLight(r); break;
                default:
                    Debug::Logger::Error("Scene", "cscene load: unknown kind %u in '%s'",
                                         static_cast<unsigned>(kind), path.c_str());
                    return {};
            }
            if (!r.Ok()) {
                Debug::Logger::Error("Scene", "cscene load: truncated object %u in '%s'",
                                     i, path.c_str());
                return {};
            }
            if (const auto* emitter = std::get_if<Particles::ParticleEmitterDesc>(
                    &parsed.descriptor)) {
                if (emitter->capacity > kMaxTotalParticleCapacity - totalParticleCapacity) {
                    Debug::Logger::Error(
                        "Scene", "cscene load: particle capacity budget exceeded in '%s'",
                        path.c_str());
                    return {};
                }
                totalParticleCapacity += emitter->capacity;
            }
            parsedNodes.push_back(std::move(parsed));
        }

        if (!r.AtEnd()) {
            Debug::Logger::Error("Scene", "cscene load: trailing or truncated data in '%s'",
                                 path.c_str());
            return {};
        }

        SceneLoadResult result;
        std::vector<std::unique_ptr<Object::Node>> nodes;
        nodes.reserve(parsedNodes.size());
        result.nodes.reserve(parsedNodes.size());
        for (ParsedNode& parsed : parsedNodes) {
            std::unique_ptr<Object::Node> node = CreateNode(std::move(parsed));
            result.nodes.push_back(node.get());
            nodes.push_back(std::move(node));
        }

        scene.CommitLoadedNodes(environment, std::move(nodes));
        result.ok = true;
        Debug::Logger::Info("Scene", "loaded '%s' (%u objects)", path.c_str(), count);
        return result;
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Scene", "cscene load: '%s' failed (%s)",
                             path.c_str(), exception.what());
        return {};
    } catch (...) {
        Debug::Logger::Error("Scene", "cscene load: '%s' failed", path.c_str());
        return {};
    }
}

} // namespace Concord
