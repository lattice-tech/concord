#ifndef CONCORD_RESOLUTION_H
#define CONCORD_RESOLUTION_H

namespace Concord {

/**
 * A window/viewport size in pixels.
 *
 * Kept as its own small aggregate (rather than two loose ints) so callers
 * can build one inline with a designated initializer, e.g.
 * `Resolution{.width = 1920, .height = 1080}`, and so the pair can be
 * reused anywhere else a pixel size is needed later without repeating two
 * bare `int` parameters.
 */
struct Resolution {
    int width = 1280;
    int height = 720;
};

} // namespace Concord

#endif // CONCORD_RESOLUTION_H
