#ifndef CONCORD_COOKEDSCENEPAYLOAD_H
#define CONCORD_COOKEDSCENEPAYLOAD_H

#include "engine/asset/id/AssetId.h"
#include "engine/collision/CollisionShape.h"
#include "engine/material/BlendMode.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/object/ReflectionMode.h"
#include "engine/object/light/SunLightDesc.h"
#include "engine/particles/EmitterShape.h"
#include "engine/particles/ParticleBurst.h"
#include "engine/particles/ParticleForceField.h"
#include "engine/particles/ParticleSimulationBackend.h"
#include "engine/render/frame/Projection.h"
#include "engine/render/frame/RenderLight.h"
#include "math/Vector3.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace Concord::Asset {

/** Stable tags for the node payloads supported by cooked scene graphs. */
enum class CookedNodeKind : std::uint8_t {
    Primitive = 0,
    Light,
    SunLight,
    Camera,
    Model,
    Collider,
    ParticleEmitter,
    PrefabInstance,
};

/** Render settings shared by every cooked node kind. */
struct CookedNodeSettings {
    Object::ReflectionMode reflectionMode = Object::ReflectionMode::Standard;
    float reflectivity = 1.0f;
};

/** Built-in primitive geometry with an optional cooked material reference. */
struct CookedPrimitivePayload {
    Object::PrimitiveShape shape = Object::PrimitiveShape::Cube;
    Vector3 size{1.0f, 1.0f, 1.0f};
    AssetId material;
};

/** Runtime-ready authored light values; placement lives on the graph node. */
struct CookedLightPayload {
    LightType type = LightType::Directional;
    Vector3 direction{0.0f, -1.0f, 0.0f};
    std::uint32_t color = 0xffffffffu;
    float intensity = 1.0f;
    float range = 20.0f;
    float sourceRadius = 0.4f;
    float directionalAngularRadiusDegrees = kDefaultDirectionalAngularRadiusDegrees;
    float innerAngleDegrees = 25.0f;
    float outerAngleDegrees = 35.0f;
    bool castShadow = false;
};

/** Complete sunlight authoring state without a duplicated transform. */
struct CookedSunLightPayload {
    Object::SunTimeMode timeMode = Object::SunTimeMode::ApparentSolar;
    float localSolarTimeHours = 12.0f;
    float civilTimeHours = 12.0f;
    float latitudeDegrees = 35.0f;
    float longitudeDegrees = 0.0f;
    float timeZoneHours = 0.0f;
    int dayOfYear = 172;
    int year = 2024;
    int month = 6;
    int day = 20;
    float manualElevationDegrees = 45.0f;
    float manualAzimuthDegrees = 180.0f;
    float northYawDegrees = 0.0f;
    float maximumIntensity = 5.0f;
    float turbidity = 2.0f;
    bool overrideColorEnabled = false;
    std::uint32_t overrideColor = 0xffffffffu;
    bool overrideIntensityEnabled = false;
    float overrideIntensity = 5.0f;
    float directionalAngularRadiusDegrees = kDefaultDirectionalAngularRadiusDegrees;
    bool visibleDisk = true;
    float visibleDiskIntensity = 1.0f;
    bool castShadow = true;
};

/** Camera projection and free-look state; placement lives on the graph node. */
struct CookedCameraPayload {
    Vector3 up{0.0f, 1.0f, 0.0f};
    Projection projection = Projection::Perspective;
    float fovYDegrees = 60.0f;
    float orthoHeight = 10.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/** One independently drawable cooked mesh and its optional material. */
struct CookedModelPart {
    AssetId mesh;
    AssetId material;
};

/** Runtime model references after source import options have been baked out. */
struct CookedModelPayload {
    std::vector<CookedModelPart> parts;
    AssetId skeleton;
    std::vector<AssetId> animations;
};

/** Collision geometry and filtering state. */
struct CookedColliderPayload {
    Collision::CollisionShape shape{};
    std::uint32_t layer = 1u;
    std::uint32_t mask = 1u;
};

/** Complete particle authoring state without a duplicated transform. */
struct CookedParticlePayload {
    Particles::EmitterShape shape = Particles::EmitterShape::Point;
    Vector3 shapeSize{1.0f, 1.0f, 1.0f};
    float shapeAngleDegrees = 30.0f;
    float emissionRate = 30.0f;
    float duration = 0.0f;
    std::vector<Particles::ParticleBurst> bursts;
    bool loop = true;
    bool prewarm = false;
    float lifetimeMin = 1.5f;
    float lifetimeMax = 1.5f;
    Vector3 direction{0.0f, 1.0f, 0.0f};
    float speedMin = 3.0f;
    float speedMax = 3.0f;
    float spreadDegrees = 15.0f;
    Vector3 gravity{0.0f, -3.0f, 0.0f};
    float drag = 0.0f;
    Vector3 rotationVelocityMin{};
    Vector3 rotationVelocityMax{};
    float turbulenceStrength = 0.0f;
    float turbulenceFrequency = 1.0f;
    std::vector<Particles::ParticleForceField> forceFields;
    bool groundCollision = false;
    float groundY = 0.0f;
    float bounce = 0.3f;
    float groundFriction = 0.8f;
    float maxSpeed = 0.0f;
    float inheritEmitterVelocity = 0.0f;
    std::uint32_t colorStart = 0xffffffffu;
    std::uint32_t colorMid = 0xffffffffu;
    std::uint32_t colorEnd = 0xffffff00u;
    float sizeStart = 0.15f;
    float sizeMid = 0.08f;
    float sizeEnd = 0.02f;
    Object::PrimitiveShape primitiveShape = Object::PrimitiveShape::Sphere;
    bool billboard = true;
    std::uint32_t capacity = 256u;
    bool unlit = true;
    Material::BlendMode blend = Material::BlendMode::Additive;
    float brightness = 2.0f;
    bool localSpace = false;
    Particles::ParticleSimulationBackend simulationBackend =
        Particles::ParticleSimulationBackend::Cpu;
    std::uint32_t seed = 0xA5F3B21Cu;
};

/** Reference to an immutable Prefab expanded beneath this Scene node at load. */
struct CookedPrefabInstancePayload {
    AssetId prefab;
};

/** Type-safe payload carried by one cooked scene-graph node. */
using CookedNodePayload = std::variant<
    CookedPrimitivePayload, CookedLightPayload, CookedSunLightPayload,
    CookedCameraPayload, CookedModelPayload, CookedColliderPayload,
    CookedParticlePayload, CookedPrefabInstancePayload>;

} // namespace Concord::Asset

#endif // CONCORD_COOKEDSCENEPAYLOAD_H
