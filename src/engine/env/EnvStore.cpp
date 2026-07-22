#include "engine/env/EnvStore.h"

#include <unordered_map>
#include <utility>

namespace Concord {

namespace {

/**
 * Function-local storage, so the map is constructed on first use and its
 * lifetime is independent of static-initialization order across the DLL.
 */
std::unordered_map<std::string, std::string>& Storage()
{
    static std::unordered_map<std::string, std::string> values;
    return values;
}

} // namespace

void EnvStore::Set(const std::string& name, std::string value)
{
    Storage()[name] = std::move(value);
}

EnvValue EnvStore::Get(const std::string& name)
{
    const auto it = Storage().find(name);
    if (it == Storage().end()) {
        return EnvValue{};
    }
    return EnvValue{it->second};
}

void EnvStore::Clear()
{
    Storage().clear();
}

} // namespace Concord
