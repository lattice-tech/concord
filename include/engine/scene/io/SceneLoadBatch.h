#ifndef CONCORD_SCENELOADBATCH_H
#define CONCORD_SCENELOADBATCH_H

#include "engine/object/PersistentObjectId.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <memory>
#include <cstdint>
#include <vector>

namespace Concord::Object {
class Node;
}

namespace Concord::Detail {

struct LoadedNodeIdentity {
    Object::PersistentObjectId id{};
    Object::PersistentObjectId parentId{};
};

struct SceneLoadBatch {
    std::uint32_t sourceVersion = 0;
    SkyEnvironment environment{};
    std::vector<std::unique_ptr<Object::Node>> nodes;
    std::vector<LoadedNodeIdentity> identities;
    Object::PersistentObjectId activeCameraId{};
    bool hasActiveCamera = false;
};

} // namespace Concord::Detail

#endif // CONCORD_SCENELOADBATCH_H
