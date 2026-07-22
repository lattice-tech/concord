#ifndef CONCORD_GAMECONFIGLOADER_H
#define CONCORD_GAMECONFIGLOADER_H

#include "engine/app/GameConfig.h"

#include <string>

namespace Concord {

/**
 * Loads a GameConfig from a small `key=value` text file.
 *
 * The format is intentionally minimal (one `key=value` pair per line, `#`
 * starts a comment, blank lines are ignored) so loading a config never
 * pulls in a third-party parser. Unrecognized keys are ignored, so the
 * file format stays forward-compatible as new GameConfig fields appear.
 */
class GameConfigLoader {
public:
    /**
     * @param path Config file path, resolved relative to the current working directory.
     * @param fallback Values used for any key absent from the file, or for
     *        every key when the file can't be opened at all.
     * @return The resulting config; this never fails outright, since a
     *         missing or unreadable file simply yields `fallback` unchanged.
     */
    static GameConfig LoadFromFile(const std::string& path, const GameConfig& fallback = GameConfig{});
};

} // namespace Concord

#endif // CONCORD_GAMECONFIGLOADER_H
