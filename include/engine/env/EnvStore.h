#ifndef CONCORD_ENVSTORE_H
#define CONCORD_ENVSTORE_H

#include "engine/env/EnvValue.h"

#include <string>

namespace Concord {

/**
 * Process-wide registry backing the global environment variables.
 *
 * This is engine-internal plumbing: the config loader fills it while
 * reading `env`-prefixed keys, and Concord::Env reads it back. Application
 * code never touches this type directly — it goes through Concord::Env.
 */
class EnvStore {
public:
    /** Stores (or overwrites) `name`'s value. `name` excludes the `env` prefix. */
    static void Set(const std::string& name, std::string value);

    /** Looks up `name`; returns an empty EnvValue when it was never set. */
    static EnvValue Get(const std::string& name);

    /** Drops every stored variable, so a fresh config load starts clean. */
    static void Clear();
};

} // namespace Concord

#endif // CONCORD_ENVSTORE_H
