#ifndef CONCORD_SPRITEDESC_H
#define CONCORD_SPRITEDESC_H

#include "engine/object/Transform.h"
#include "math/Vector2.h"

#include <string>

namespace Concord::Object {

/**
 * Everything a Sprite node is built from: a texture atlas laid out as a
 * `columns` x `rows` grid of equal cells, played back as a frame animation.
 *
 * A plain aggregate. The sprite draws a flat world-space quad (in the node's
 * local XY plane) textured by the current frame's cell; the frame advances at
 * `fps`, looping over `frameCount` cells in row-major order.
 */
struct SpriteDesc {
    /** Where the quad sits (parent-relative); the quad spans the local XY plane. */
    Transform transform{};

    /** Atlas image path (PNG/JPG), a columns x rows grid of frames. */
    std::string texture;

    /** Atlas grid dimensions. */
    int columns = 1;
    int rows = 1;

    /** Number of animation frames (row-major); 0 means columns*rows. */
    int frameCount = 0;

    /** Playback rate in frames per second. */
    float fps = 10.0f;

    /** World-space size of the quad (width, height). */
    Vector2 size{1.0f, 1.0f};

    /** Loop the frame sequence (true) or hold the last frame (false). */
    bool loop = true;

    /** Unlit (emit the texel directly, default) or shade with scene lights. */
    bool unlit = true;
};

} // namespace Concord::Object

#endif // CONCORD_SPRITEDESC_H
