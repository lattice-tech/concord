#ifndef CONCORD_CONFIGLOCATOR_H
#define CONCORD_CONFIGLOCATOR_H

#include "Concord/CExport.h"

#include <string>

namespace Concord {

/**
 * Decides which config file the engine loads.
 *
 * By default the engine searches a small list of well-known locations (the
 * current working directory first, then the executable's own directory) for
 * `Concord.cfg`, so a game shipped next to its config "just works" no matter
 * where it is launched from. An application may override this entirely by
 * calling SetOverridePath before constructing its Game — the override is used
 * verbatim and the search is skipped.
 *
 * This is process-wide static state on purpose: the config location is a
 * launch-time decision shared by every Game in the process.
 */
class CENGINE_API ConfigLocator {
public:
    /**
     * Forces a specific config file path, bypassing the default search.
     * Pass an empty string to clear the override and restore default search.
     */
    static void SetOverridePath(const std::string& path);

    /** The current override, or an empty string when none is set. */
    static std::string OverridePath();

    /** The config filename the default search looks for (`Concord.cfg`). */
    static const char* DefaultFileName() noexcept;

    /**
     * Resolves the config path to load.
     *
     * @param hint A caller-supplied path (e.g. the Game constructor argument).
     *        When non-empty it takes precedence over everything else.
     * @return, in order of precedence: `hint` if non-empty; else the override
     *         set via SetOverridePath if any; else the first default location
     *         that exists; else the bare default filename (so the loader still
     *         reports a sensible "not found" against the working directory).
     */
    static std::string Resolve(const std::string& hint = {});
};

} // namespace Concord

#endif // CONCORD_CONFIGLOCATOR_H
