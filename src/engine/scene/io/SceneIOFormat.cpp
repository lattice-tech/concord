#include "engine/scene/io/SceneIOFormat.h"

#include "engine/object/Camera.h"
#include "engine/particles/ParticleEmitter.h"
#include "engine/scene/io/SceneIOCodec.h"
#include "engine/scene/io/SceneIOPayload.h"

#include <limits>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Concord::Detail::SceneIo {

namespace {

struct ParsedRecord {
    Object::PersistentObjectId id{};
    Object::PersistentObjectId parentId{};
    ParsedNode node;
};

bool ReadRecord(Reader& reader, bool framed, std::uint64_t generatedId,
                ParsedRecord& record)
{
    Reader* payload = &reader;
    Reader frame(nullptr, 0);
    if (framed) {
        const std::uint32_t frameSize = reader.GetU32();
        frame = reader.GetFrame(frameSize);
        payload = &frame;
        record.id.value = payload->GetU64();
        record.parentId.value = payload->GetU64();
    } else {
        record.id.value = generatedId;
    }
    const auto kind = static_cast<NodeKind>(payload->GetU8());
    if (!ReadNodePayload(*payload, kind, record.node)) {
        return false;
    }
    if (framed && !payload->AtEnd()) {
        return false;
    }
    return reader.Ok() && payload->Ok();
}

bool ValidateGraph(const std::vector<ParsedRecord>& records,
                   Object::PersistentObjectId activeCameraId)
{
    std::unordered_map<std::uint64_t, std::size_t> indices;
    indices.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        const std::uint64_t id = records[index].id.value;
        if (id == 0 || !indices.emplace(id, index).second) return false;
    }
    for (const ParsedRecord& record : records) {
        if (record.parentId.value == record.id.value) return false;
        if (record.parentId.IsValid() && !indices.contains(record.parentId.value)) return false;
    }

    std::vector<std::size_t> parents(records.size(), records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto parent = indices.find(records[index].parentId.value);
        if (parent != indices.end()) parents[index] = parent->second;
    }
    std::vector<std::uint8_t> states(records.size(), 0);
    for (std::size_t start = 0; start < records.size(); ++start) {
        std::size_t current = start;
        while (current != records.size() && states[current] == 0) {
            states[current] = 1;
            current = parents[current];
        }
        if (current != records.size() && states[current] == 1) return false;
        current = start;
        while (current != records.size() && states[current] == 1) {
            states[current] = 2;
            current = parents[current];
        }
    }

    if (activeCameraId.IsValid()) {
        const auto active = indices.find(activeCameraId.value);
        if (active == indices.end()
            || !std::holds_alternative<ParsedCamera>(records[active->second].node.descriptor)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> EncodeV7(const SaveSnapshot& snapshot)
{
    if (snapshot.nodes.size() > kMaxSceneNodes) {
        throw std::length_error("scene exceeds the node budget");
    }
    Writer writer;
    writer.PutU32(kMagic); writer.PutU32(kVersion7);
    WriteSkyEnvironment(writer, snapshot.environment);
    writer.PutU64(snapshot.activeCameraId.value);
    writer.PutU32(static_cast<std::uint32_t>(snapshot.nodes.size()));

    std::unordered_set<std::uint64_t> ids;
    std::uint32_t totalParticleCapacity = 0;
    for (const SaveNode& entry : snapshot.nodes) {
        if (!entry.id.IsValid() || !ids.insert(entry.id.value).second) {
            throw std::runtime_error("scene contains invalid or duplicate persistent IDs");
        }
        NodeKind kind;
        if (!GetNodeKind(*entry.node, kind)) {
            throw std::runtime_error("scene snapshot contains an unsupported node");
        }
        if (kind == NodeKind::ParticleEmitter) {
            const auto& emitter = static_cast<const Particles::ParticleEmitter&>(*entry.node);
            ValidateParticleForSave(emitter.Desc());
            if (emitter.Desc().capacity > kMaxTotalParticleCapacity - totalParticleCapacity) {
                throw std::length_error("scene exceeds the particle capacity budget");
            }
            totalParticleCapacity += emitter.Desc().capacity;
        }
        const std::size_t sizeOffset = writer.bytes.size();
        writer.PutU32(0);
        const std::size_t recordOffset = writer.bytes.size();
        writer.PutU64(entry.id.value); writer.PutU64(entry.parentId.value);
        writer.PutU8(static_cast<std::uint8_t>(kind));
        WriteNodePayload(writer, kind, *entry.node);
        const std::size_t recordSize = writer.bytes.size() - recordOffset;
        if (recordSize > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("scene node record exceeds the framing field");
        }
        writer.PatchU32(sizeOffset, static_cast<std::uint32_t>(recordSize));
        writer.EnforceFileBudget();
    }
    if (snapshot.activeCameraId.IsValid() && !ids.contains(snapshot.activeCameraId.value)) {
        throw std::runtime_error("active camera is absent from the scene snapshot");
    }
    for (const SaveNode& entry : snapshot.nodes) {
        if (entry.parentId.IsValid() && !ids.contains(entry.parentId.value)) {
            throw std::runtime_error("scene node parent is absent from the scene snapshot");
        }
    }
    return std::move(writer.bytes);
}

bool Decode(const std::vector<std::uint8_t>& bytes, SceneLoadBatch& batch,
            std::uint32_t& version)
{
    Reader reader(bytes.data(), bytes.size());
    if (reader.GetU32() != kMagic) return false;
    version = reader.GetU32();
    if (version != kVersion6 && version != kVersion7) return false;
    batch.sourceVersion = version;
    batch.environment = ReadSkyEnvironment(reader);
    if (!reader.Ok()) return false;

    if (version == kVersion7) {
        batch.activeCameraId.value = reader.GetU64();
        batch.hasActiveCamera = batch.activeCameraId.IsValid();
    }
    const std::uint32_t count = reader.GetU32();
    const std::size_t minimum = version == kVersion7 ? 4u + 8u + 8u + 1u : 1u + 1u + 4u;
    if (!ValidateCount(reader, count, kMaxSceneNodes, minimum)) return false;

    std::vector<ParsedRecord> records;
    records.reserve(count);
    std::uint32_t totalParticleCapacity = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        ParsedRecord record;
        if (!ReadRecord(reader, version == kVersion7,
                        static_cast<std::uint64_t>(index) + 1u, record)) return false;
        const std::uint32_t capacity = ParticleCapacity(record.node);
        if (capacity > kMaxTotalParticleCapacity - totalParticleCapacity) return false;
        totalParticleCapacity += capacity;
        records.push_back(std::move(record));
    }
    if (!reader.AtEnd() || !ValidateGraph(records, batch.activeCameraId)) return false;

    batch.nodes.reserve(records.size()); batch.identities.reserve(records.size());
    for (ParsedRecord& record : records) {
        batch.identities.push_back({record.id, record.parentId});
        batch.nodes.push_back(CreateNode(std::move(record.node)));
    }
    return true;
}

} // namespace Concord::Detail::SceneIo
