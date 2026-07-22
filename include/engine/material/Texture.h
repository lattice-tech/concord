#ifndef CONCORD_TEXTURE_H
#define CONCORD_TEXTURE_H

#include <string>

namespace Concord::Material {

/**
 * A reference to an image file that paints one channel of a material.
 *
 * A material composes several of these (see MaterialTextures): an albedo map,
 * a normal map, and so on. The reference is by file path and resolved lazily
 * by the renderer, which decodes and uploads the image once and shares the
 * resulting GPU texture across every material that names the same path. An
 * empty `path` means "no texture for this channel", so the surface falls back
 * to its flat parameters.
 *
 * A plain aggregate so a caller writes `.albedo = {.path = "brick.png"}`.
 */
struct Texture {
    /**
     * Path to the image file (PNG, JPG, KTX, DDS, ...), relative to the
     * working directory or absolute. Empty means the channel is unused.
     */
    std::string path;
};

} // namespace Concord::Material

#endif // CONCORD_TEXTURE_H
