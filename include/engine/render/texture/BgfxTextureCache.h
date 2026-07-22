#ifndef CONCORD_BGFXTEXTURECACHE_H
#define CONCORD_BGFXTEXTURECACHE_H

#include "engine/render/texture/TextureId.h"

#include <bgfx/bgfx.h>

#include <unordered_map>

namespace Concord {

/**
 * The render thread's cache of GPU textures, keyed by TextureId.
 *
 * A material names its maps by path; ResolveMaterial interns those paths to
 * TextureIds (see TextureRegistry). This cache turns an id into a live bgfx
 * texture: on first request it looks the path back up, decodes the image with
 * bimg, and uploads it once, so every material that shares a path shares one
 * GPU texture. A failed load is remembered as an invalid handle so a broken
 * path is reported once, not every frame (Requirement 5.8).
 *
 * Lives on the render thread and is owned by the backend; every bgfx call it
 * makes therefore runs on the correct thread. Clear() releases everything and
 * must be called from the backend's Shutdown before bgfx::shutdown.
 */
class BgfxTextureCache {
public:
    /**
     * Returns the GPU texture for `id`, loading it on first use. Returns an
     * invalid handle for TextureId::None or a path that failed to load; callers
     * substitute White() so a sampler is always bound.
     */
    bgfx::TextureHandle Get(TextureId id);

    /** A 1x1 opaque white texture, created on first use; the neutral fallback. */
    bgfx::TextureHandle White();

    /** Destroys every cached texture (including the white fallback). */
    void Clear();

private:
    std::unordered_map<TextureId, bgfx::TextureHandle> m_textures;
    bgfx::TextureHandle m_white = BGFX_INVALID_HANDLE;
};

} // namespace Concord

#endif // CONCORD_BGFXTEXTURECACHE_H
