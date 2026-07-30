#include "engine/asset/cook/CookedPrefab.h"

#include "engine/asset/cook/CookedSceneCodec.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <stdexcept>
#include <utility>

namespace Concord::Asset::CookedPrefab {

namespace {

using Detail::CookedSceneCodec::DecodedGraphNode;
using Detail::CookedSceneCodec::GraphNodeRef;
using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x42465043u; // 'CPFB' in file order
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kHeaderBytes = sizeof(std::uint32_t) * 2u;

std::vector<GraphNodeRef> NodeRefs(const CookedPrefabData& prefab)
{
    std::vector<GraphNodeRef> refs;
    refs.reserve(prefab.nodes.size());
    for (const CookedPrefabNode& node : prefab.nodes) {
        refs.push_back({node.id.value, node.parentId.value, &node.data});
    }
    return refs;
}

} // namespace

bool Validate(const CookedPrefabData& prefab,
              const CookedSceneGraphLimits& limits)
{
    for (const CookedPrefabNode& node : prefab.nodes) {
        if (std::holds_alternative<CookedPrefabInstancePayload>(node.data.payload)) {
            return false;
        }
    }
    const std::vector<GraphNodeRef> refs = NodeRefs(prefab);
    std::size_t graphBytes = 0;
    return Detail::CookedSceneCodec::ValidateGraph(
               refs, 0u, false, limits, &graphBytes)
        && kHeaderBytes <= limits.maxFileBytes
        && graphBytes <= limits.maxFileBytes - kHeaderBytes;
}

std::vector<std::uint8_t> Encode(const CookedPrefabData& prefab,
                                 const CookedSceneGraphLimits& limits)
{
    if (!Validate(prefab, limits)) {
        throw std::invalid_argument("prefab violates the cooked format contract");
    }
    const std::vector<GraphNodeRef> refs = NodeRefs(prefab);
    BinaryWriter writer;
    writer.PutU32(kMagic); writer.PutU32(kVersion);
    Detail::CookedSceneCodec::WriteGraph(writer, refs);
    return writer.Take();
}

std::optional<CookedPrefabData> Decode(
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
    std::vector<DecodedGraphNode> decoded;
    if (!Detail::CookedSceneCodec::ReadGraph(reader, decoded, limits)
        || !reader.AtEnd()) return std::nullopt;
    CookedPrefabData prefab;
    prefab.nodes.reserve(decoded.size());
    for (DecodedGraphNode& node : decoded) {
        prefab.nodes.push_back({{node.id}, {node.parentId}, std::move(node.data)});
    }
    if (!Validate(prefab, limits)) return std::nullopt;
    return prefab;
}

} // namespace Concord::Asset::CookedPrefab
