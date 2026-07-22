#ifndef CONCORD_ENV_H
#define CONCORD_ENV_H

#include "Concord/CExport.h"
#include "engine/env/EnvValue.h"

#include <string>

namespace Concord {

/**
 * Reads a global environment variable defined in the engine config.
 *
 * Variables are declared in Concord.cfg with an `env` prefix (e.g.
 * `envVersion="1.0"`) and read back here *without* it, so `envVersion`
 * becomes `Env("Version")`. An unset name yields an empty EnvValue rather
 * than an error, so lookups are always safe.
 *
 * @param name Variable name without the `env` prefix (case-sensitive).
 * @return The stored value, or an empty EnvValue if `name` was never set.
 */
CENGINE_API EnvValue Env(const std::string& name);

} // namespace Concord

#endif // CONCORD_ENV_H
