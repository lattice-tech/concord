#ifndef CONCORD_MSAALEVEL_H
#define CONCORD_MSAALEVEL_H

namespace Concord {

/**
 * Multisample anti-aliasing level for a window's swap chain.
 *
 * bgfx applies MSAA process-wide through bgfx::reset, so when several
 * windows are open the most recent AttachWindow or Set() that names an
 * msaa value wins for every window (same "last write" model as showCursor).
 */
enum class MsaaLevel {
    Off = 0,
    X2 = 2,
    X4 = 4,
    X8 = 8,
    X16 = 16,
};

} // namespace Concord

#endif // CONCORD_MSAALEVEL_H
