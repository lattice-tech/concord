#ifndef CONCORD_SCENEOBJECTSLOT_H
#define CONCORD_SCENEOBJECTSLOT_H

#include <cstdint>

namespace Concord::Object {
class Node;
}

namespace Concord::Detail {

enum class SceneObjectState : std::uint8_t {
    Free,
    Live,
    PendingDespawn,
    Exhausted,
};

struct SceneObjectSlot {
    Object::Node* node = nullptr;
    std::uint32_t generation = 1;
    SceneObjectState state = SceneObjectState::Free;
};

} // namespace Concord::Detail

#endif // CONCORD_SCENEOBJECTSLOT_H
