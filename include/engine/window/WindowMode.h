#ifndef CONCORD_WINDOWMODE_H
#define CONCORD_WINDOWMODE_H

namespace Concord {

/**
 * How a window is presented on screen.
 *
 * Applied when the window is first attached and whenever Set() changes it, so
 * a window can toggle between these live (see docs/窗口.md).
 */
enum class WindowMode {
    /** A normal, decorated, resizable-by-the-OS window at the requested size. */
    Windowed,

    /** Desktop fullscreen: the window covers the whole display, no video-mode switch. */
    Fullscreen,

    /** A borderless window (no title bar or frame) at the requested size. */
    Borderless,
};

/** Canonical, human-readable name of a WindowMode (never null). */
inline const char* ToString(WindowMode mode)
{
    switch (mode) {
        case WindowMode::Windowed:   return "Windowed";
        case WindowMode::Fullscreen: return "Fullscreen";
        case WindowMode::Borderless: return "Borderless";
    }
    return "Windowed";
}

} // namespace Concord

#endif // CONCORD_WINDOWMODE_H
