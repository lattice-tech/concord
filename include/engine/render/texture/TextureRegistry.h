#ifndef CONCORD_TEXTUREREGISTRY_H
#define CONCORD_TEXTUREREGISTRY_H

#include "engine/render/texture/TextureId.h"

#include <string>

namespace Concord {

/**
 * The process-wide intern table mapping texture file paths to TextureIds.
 *
 * Material resolution runs on application threads and must not touch the
 * graphics API, so it cannot load textures directly. Instead it interns each
 * path here — a cheap, thread-safe string-to-id lookup that neither reads the
 * file nor allocates a GPU resource — and stores the returned TextureId in the
 * resolved material. The render thread's texture cache later turns that id back
 * into a path (Path) and decodes/uploads the image once, sharing it across
 * every material that named the same path.
 *
 * The mapping is monotonic and never freed for the life of the process: paths
 * are few and small, and stable ids let the cache key on a plain integer.
 */
class TextureRegistry {
public:
    /**
     * Returns the id for `path`, assigning a new one the first time a path is
     * seen. An empty path always maps to TextureId::None. Thread-safe.
     */
    static TextureId Acquire(const std::string& path);

    /**
     * Returns the path `id` was interned from, or an empty string for
     * TextureId::None or an id this process never handed out. Thread-safe.
     */
    static std::string Path(TextureId id);
};

} // namespace Concord

#endif // CONCORD_TEXTUREREGISTRY_H
