#ifndef CONCORD_WINDOWRESIZEEDGE_H
#define CONCORD_WINDOWRESIZEEDGE_H

namespace Concord {

/**
 * Which edge or corner of a window a custom resize gesture controls.
 */
enum class WindowResizeEdge {
    Left,
    Top,
    Right,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

} // namespace Concord

#endif // CONCORD_WINDOWRESIZEEDGE_H
