#ifndef CONCORD_SCENEIOFORMAT_H
#define CONCORD_SCENEIOFORMAT_H

#include "engine/object/PersistentObjectId.h"
#include "engine/scene/io/SceneLoadBatch.h"

#include <cstdint>
#include <vector>

namespace Concord::Object {
class Node;
}

namespace Concord::Detail::SceneIo {

struct SaveNode {
    const Object::Node* node = nullptr;
    Object::PersistentObjectId id{};
    Object::PersistentObjectId parentId{};
};

struct SaveSnapshot {
    SkyEnvironment environment{};
    std::vector<SaveNode> nodes;
    Object::PersistentObjectId activeCameraId{};
};

/**
 * @brief Encodes CSCN v7 using explicit little-endian scalar fields.
 *
 * Layout: magic:u32, version:u32, legacy sky payload, activeCameraId:u64,
 * nodeCount:u32, followed by nodeCount framed records. Each record is
 * recordBytes:u32, persistentId:u64, parentId:u64, kind:u8, then the exact
 * node-settings and type payload used by CSCN v6.
 */
std::vector<std::uint8_t> EncodeV7(const SaveSnapshot& snapshot);

/** Parses CSCN v6 or v7 and validates all graph references before construction. */
bool Decode(const std::vector<std::uint8_t>& bytes, SceneLoadBatch& batch,
            std::uint32_t& version);

} // namespace Concord::Detail::SceneIo

#endif // CONCORD_SCENEIOFORMAT_H
