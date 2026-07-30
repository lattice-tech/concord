#include "engine/asset/cook/CookedSceneCodec.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace Concord::Asset::Detail::CookedSceneCodec {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::size_t kNodeIdentityBytes = sizeof(std::uint64_t) * 2u;
constexpr std::size_t kMinimumNodeDataBytes = 46u;
constexpr std::size_t kMinimumFramedNodeBytes = sizeof(std::uint32_t)
    + kNodeIdentityBytes + kMinimumNodeDataBytes;

bool AddSize(std::size_t amount, std::size_t limit, std::size_t& total) noexcept
{
    if (amount > limit || total > limit - amount) return false;
    total += amount;
    return true;
}

} // namespace

bool ValidateGraph(std::span<const GraphNodeRef> nodes,
                   std::uint64_t activeCameraId, bool requireActiveCameraKind,
                   const CookedSceneGraphLimits& limits,
                   std::size_t* encodedBytes)
{
    if (nodes.size() > limits.maxNodes || limits.maxHierarchyDepth == 0u) {
        return false;
    }

    std::unordered_map<std::uint64_t, std::size_t> indices;
    indices.reserve(nodes.size());
    DecodeBudget budget;
    std::size_t totalBytes = sizeof(std::uint32_t);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const GraphNodeRef& node = nodes[index];
        if (node.id == 0u || node.data == nullptr
            || !indices.emplace(node.id, index).second) return false;
        std::size_t nodeDataBytes = 0;
        if (!ValidatePayload(*node.data, limits, budget, nodeDataBytes)
            || !AddSize(sizeof(std::uint32_t) + kNodeIdentityBytes + nodeDataBytes,
                        limits.maxFileBytes, totalBytes)) return false;
    }

    std::vector<std::size_t> parents(nodes.size(), nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const GraphNodeRef& node = nodes[index];
        if (node.parentId == node.id) return false;
        if (node.parentId != 0u) {
            const auto parent = indices.find(node.parentId);
            if (parent == indices.end()) return false;
            parents[index] = parent->second;
        }
    }

    std::vector<std::uint8_t> states(nodes.size(), 0u);
    std::vector<std::uint32_t> depths(nodes.size(), 0u);
    std::vector<std::size_t> path;
    path.reserve(std::min<std::size_t>(nodes.size(), limits.maxHierarchyDepth));
    for (std::size_t start = 0; start < nodes.size(); ++start) {
        if (states[start] == 2u) continue;
        path.clear();
        std::size_t current = start;
        while (current != nodes.size() && states[current] == 0u) {
            states[current] = 1u;
            path.push_back(current);
            if (path.size() > limits.maxHierarchyDepth) return false;
            current = parents[current];
        }
        if (current != nodes.size() && states[current] == 1u) return false;
        std::uint32_t depth = current == nodes.size() ? 0u : depths[current];
        for (auto item = path.rbegin(); item != path.rend(); ++item) {
            if (depth >= limits.maxHierarchyDepth) return false;
            depths[*item] = ++depth;
            states[*item] = 2u;
        }
    }

    if (activeCameraId != 0u) {
        const auto active = indices.find(activeCameraId);
        if (active == indices.end()) return false;
        if (requireActiveCameraKind
            && !std::holds_alternative<CookedCameraPayload>(
                nodes[active->second].data->payload)) return false;
    }
    if (encodedBytes != nullptr) *encodedBytes = totalBytes;
    return true;
}

void WriteGraph(BinaryWriter& writer, std::span<const GraphNodeRef> nodes)
{
    std::vector<GraphNodeRef> ordered(nodes.begin(), nodes.end());
    std::sort(ordered.begin(), ordered.end(), [](const GraphNodeRef& left,
                                                 const GraphNodeRef& right) {
        return left.id < right.id;
    });
    writer.PutU32(static_cast<std::uint32_t>(ordered.size()));
    for (const GraphNodeRef& node : ordered) {
        const std::size_t sizeOffset = writer.Size();
        writer.PutU32(0u);
        const std::size_t recordOffset = writer.Size();
        writer.PutU64(node.id); writer.PutU64(node.parentId);
        WriteNodeData(writer, *node.data);
        const std::size_t recordBytes = writer.Size() - recordOffset;
        writer.PatchU32(sizeOffset, static_cast<std::uint32_t>(recordBytes));
    }
}

bool ReadGraph(BinaryReader& reader, std::vector<DecodedGraphNode>& nodes,
               const CookedSceneGraphLimits& limits)
{
    const std::uint32_t count = reader.GetU32();
    if (!reader.Ok() || count > limits.maxNodes
        || count > reader.Remaining() / kMinimumFramedNodeBytes) return false;

    nodes.clear();
    nodes.reserve(count);
    DecodeBudget budget;
    std::uint64_t previousId = 0u;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t frameBytes = reader.GetU32();
        if (!reader.Ok() || frameBytes < kNodeIdentityBytes + kMinimumNodeDataBytes
            || frameBytes > limits.maxNodeRecordBytes) return false;
        BinaryReader frame = reader.GetFrame(frameBytes);
        DecodedGraphNode node;
        node.id = frame.GetU64(); node.parentId = frame.GetU64();
        if (node.id == 0u || node.id <= previousId
            || !ReadNodeData(frame, limits, budget, node.data)
            || !frame.AtEnd() || !reader.Ok()) return false;
        previousId = node.id;
        nodes.push_back(std::move(node));
    }

    std::vector<GraphNodeRef> refs;
    refs.reserve(nodes.size());
    for (const DecodedGraphNode& node : nodes) {
        refs.push_back({node.id, node.parentId, &node.data});
    }
    return ValidateGraph(refs, 0u, false, limits);
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
