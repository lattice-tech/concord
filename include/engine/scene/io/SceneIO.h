#ifndef CONCORD_SCENEIO_H
#define CONCORD_SCENEIO_H

#include "Concord/CExport.h"

#include <string>
#include <vector>

namespace Concord {

class Scene;

namespace Object {
class Node;
}

/**
 * Result of loading a `.cscene`: the committed objects (non-owning, in file
 * order) so the caller can immediately drive them via the Node/Object API
 * (SetPosition, Rotate, a Mover, ...). `ok` is false on any read error.
 */
struct SceneLoadResult {
    bool ok = false;
    std::vector<Object::Node*> nodes;
};

/**
 * Binary scene serialization (`.cscene`, CSCN version 6).
 *
 * Stores a **flat** snapshot of a Scene's objects with its local transform
 * and type-specific description. The parent/child hierarchy is intentionally
 * NOT stored ("only the scene, not the levels"): every object is written and
 * reloaded at the scene root. The layout is raw little-endian POD (no text
 * parsing) for fast load/save; it is a same-endianness runtime cache format,
 * not a portable interchange format.
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
