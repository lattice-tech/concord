#ifndef CONCORD_TEXTUREID_H
#define CONCORD_TEXTUREID_H

#include <cstdint>

namespace Concord {

/**
 * A stable, lightweight reference to a texture path.
 *
 * Material descriptions name textures by file path (std::string), but the flat
 * RenderMaterial the render thread consumes must stay free of strings and
 * ownership (see RenderMaterial). A TextureId bridges the two: the
 * TextureRegistry interns each distinct path to one of these small integers,
 * which the resolved material carries and the backend's texture cache resolves
 * back to a GPU texture on the render thread.
 *
 * The reserved value 0 means "no texture" — a channel that falls back to the
 * material's flat parameter.
 */
enum class TextureId : std::uint32_t {
    None = 0,
};

/** True when `id` names a texture rather than the "no texture" sentinel. */
inline constexpr bool IsValid(TextureId id) noexcept
{
    return id != TextureId::None;
}

} // namespace Concord

#endif // CONCORD_TEXTUREID_H
