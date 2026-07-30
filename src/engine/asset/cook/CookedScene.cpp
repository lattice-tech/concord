#include "engine/asset/cook/CookedScene.h"

#include "engine/asset/cook/CookedSceneCodec.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <stdexcept>
#include <utility>

namespace Concord::Asset::CookedScene {

namespace {

using Detail::CookedSceneCodec::DecodedGraphNode;
using Detail::CookedSceneCodec::GraphNodeRef;
using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x45435343u; // 'CSCE' in file order
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kHeaderBytes = sizeof(std::uint32_t) * 2u;

std::vector<GraphNodeRef> NodeRefs(const CookedSceneData& scene)
{
    std::vector<GraphNodeRef> refs;
    refs.reserve(scene.nodes.size());
    for (const CookedSceneNode& node : scene.nodes) {
        refs.push_back({node.id.value, node.parentId.value, &node.data});
    }
    return refs;
}

} // namespace

bool Validate(const CookedSceneData& scene,
              const CookedSceneGraphLimits& limits)
{
    if (!Detail::CookedSceneCodec::ValidateEnvironment(scene.environment)) {
        return false;
    }
    const std::vector<GraphNodeRef> refs = NodeRefs(scene);
    std::size_t graphBytes = 0;
    if (!Detail::CookedSceneCodec::ValidateGraph(
            refs, scene.activeCameraId.value, true, limits, &graphBytes)) {
        return false;
    }
    BinaryWriter environment;
    Detail::CookedSceneCodec::WriteEnvironment(environment, scene.environment);
    const std::size_t fixedBytes = kHeaderBytes + environment.Size()
        + sizeof(std::uint64_t);
    return fixedBytes <= limits.maxFileBytes
        && graphBytes <= limits.maxFileBytes - fixedBytes;
}

std::vector<std::uint8_t> Encode(const CookedSceneData& scene,
                                 const CookedSceneGraphLimits& limits)
{
    if (!Validate(scene, limits)) {
        throw std::invalid_argument("scene violates the cooked format contract");
    }
    const std::vector<GraphNodeRef> refs = NodeRefs(scene);
    BinaryWriter writer;
    writer.PutU32(kMagic); writer.PutU32(kVersion);
    Detail::CookedSceneCodec::WriteEnvironment(writer, scene.environment);
    writer.PutU64(scene.activeCameraId.value);
    Detail::CookedSceneCodec::WriteGraph(writer, refs);
    return writer.Take();
}

std::optional<CookedSceneData> Decode(
    const std::uint8_t* data, std::size_t size,
    const CookedSceneGraphLimits& limits)
{
    if (data == nullptr || size < kHeaderBytes || size > limits.maxFileBytes) {
        return std::nullopt;
    }
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }
    CookedSceneData scene;
    if (!Detail::CookedSceneCodec::ReadEnvironment(reader, scene.environment)) {
        return std::nullopt;
    }
    scene.activeCameraId.value = reader.GetU64();
    std::vector<DecodedGraphNode> decoded;
    if (!reader.Ok() || !Detail::CookedSceneCodec::ReadGraph(reader, decoded, limits)
        || !reader.AtEnd()) return std::nullopt;
    scene.nodes.reserve(decoded.size());
    for (DecodedGraphNode& node : decoded) {
        scene.nodes.push_back({{node.id}, {node.parentId}, std::move(node.data)});
    }
    if (!Validate(scene, limits)) return std::nullopt;
    return scene;
}

} // namespace Concord::Asset::CookedScene
