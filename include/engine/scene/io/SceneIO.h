#ifndef CONCORD_SCENEIO_H
#define CONCORD_SCENEIO_H

#include "Concord/CExport.h"
#include "engine/object/ObjectHandle.h"

#include <string>
#include <vector>

namespace Concord {

class Scene;

namespace Object {
class Node;
}

/**
 * Result of loading a `.cscene`: committed objects and their generation-safe
 * handles in file order. Raw pointers are non-owning and intended for immediate
 * use; handles are the safe identity to retain. `ok` is false on any error.
 */
struct SceneLoadResult {
    bool ok = false;
    std::vector<Object::Node*> nodes;
    std::vector<Object::ObjectHandle> handles;
};

/**
 * Binary scene serialization (`.cscene`, loading CSCN v6 and v7).
 *
 * Save emits CSCN v7 with stable persistent IDs, parent references, active
 * camera identity, explicit little-endian fields, and framed node records.
 * Legacy v6 files retain their exact flat payload compatibility and migrate to
 * deterministic IDs 1..N in file order.
 *
 * Covered node kinds are Box, Light, SunLight, Camera, Model, Collider, and
 * ParticleEmitter. Particle runtime state is not stored.
 *
 *   Save(scene, "level.cscene");           // export the current scene
 *   Scene fresh;
 *   auto r = SceneIO::Load(fresh, "level.cscene"); // import into a scene
 *   game.LoadScene(fresh);                 // r.nodes[i] are live objects
 *
 * Loading parses and constructs the complete batch before acquiring the target
 * Scene's graph lock. A format, resource-budget, allocation, or construction
 * failure leaves the target Scene unchanged.
 */
class CENGINE_API SceneIO {
public:
    /** Writes every serializable object in `scene` to `path`. Returns false on I/O error. */
    static bool Save(const Scene& scene, const std::string& path);

    /**
     * Appends the objects stored in `path` and returns them in file order.
     * `scene` may already contain objects. Files larger than 64 MiB, scenes
     * above 65,536 nodes, oversized strings, or particle descriptors exceeding
     * their serialized resource budgets are rejected. On failure `ok` is false
     * and `scene` is unchanged.
     */
    static SceneLoadResult Load(Scene& scene, const std::string& path);
};

} // namespace Concord

#endif // CONCORD_SCENEIO_H
