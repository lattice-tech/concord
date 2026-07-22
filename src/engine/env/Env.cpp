#include "engine/env/Env.h"

#include "engine/env/EnvStore.h"

namespace Concord {

EnvValue Env(const std::string& name)
{
    return EnvStore::Get(name);
}

} // namespace Concord
