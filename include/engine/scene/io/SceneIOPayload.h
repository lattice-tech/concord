#ifndef CONCORD_SCENEIOPAYLOAD_H
#define CONCORD_SCENEIOPAYLOAD_H

#include "engine/object/Box.h"
#include "engine/object/Camera.h"
#include "engine/object/Collider.h"
#include "engine/object/Light.h"
#include "engine/object/Model.h"
#include "engine/object/light/SunLight.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "engine/scene/io/SceneIOCodec.h"

#include <memory>
#include <variant>

namespace Concord::Detail::SceneIo {

struct NodeSettings {
    Object::ReflectionMode reflectionMode = Object::ReflectionMode::Standard;
    float reflectivity = 1.0f;
};

struct ParsedCamera {
    Object::CameraDesc desc;
    Transform local;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/** A plain Object::Node pivot: its local transform is the whole payload. */
struct ParsedPivot {
    Transform local;
};

using NodeDescriptor = std::variant<
    Object::BoxDesc, Object::LightDesc, ParsedCamera, Object::ModelDesc,
    Object::ColliderDesc, Particles::ParticleEmitterDesc, Object::SunLightDesc,
    ParsedPivot>;

struct ParsedNode {
    NodeSettings settings;
    NodeDescriptor descriptor;
};

void WriteNodeSettings(Writer& writer, const Object::Node& node);
NodeSettings ReadNodeSettings(Reader& reader);
void WriteMaterial(Writer& writer, const Material::MaterialDesc& material);
Material::MaterialDesc ReadMaterial(Reader& reader);
bool GetNodeKind(const Object::Node& node, NodeKind& kind);
void WriteNodePayload(Writer& writer, NodeKind kind, const Object::Node& node);
bool ReadNodePayload(Reader& reader, NodeKind kind, ParsedNode& parsed);
void WriteParticlePayload(Writer& writer, const Particles::ParticleEmitterDesc& descriptor,
                          const Transform& transform);
Particles::ParticleEmitterDesc ReadParticlePayload(Reader& reader);
std::unique_ptr<Object::Node> CreateNode(ParsedNode parsed);
std::uint32_t ParticleCapacity(const ParsedNode& parsed) noexcept;
void ValidateParticleForSave(const Particles::ParticleEmitterDesc& descriptor);

} // namespace Concord::Detail::SceneIo

#endif // CONCORD_SCENEIOPAYLOAD_H
