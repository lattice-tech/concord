#include "engine/app/ConfigLocator.h"

#include "engine/debug/Logger.h"

#include <filesystem>
#include <mutex>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace Concord {

namespace {

constexpr const char* kDefaultFileName = "Concord.cfg";

std::mutex g_mutex;
std::string g_override;

/** Absolute directory the running executable lives in, or empty if unknown. */
std::filesystem::path ExecutableDir()
{
#if defined(_WIN32)
    char buffer[MAX_PATH];
    const DWORD length = ::GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (length == 0 || length == sizeof(buffer)) {
        return {}; // failed, or path was truncated — treat as unknown
    }
    return std::filesystem::path(std::string(buffer, length)).parent_path();
#else
    return {};
#endif
}

/** The ordered list of default locations to probe for the config file. */
std::vector<std::filesystem::path> DefaultCandidates()
{
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        candidates.push_back(cwd / kDefaultFileName);
    }
    const std::filesystem::path exeDir = ExecutableDir();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / kDefaultFileName);
    }
    return candidates;
}

} // namespace

void ConfigLocator::SetOverridePath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_override = path;
    if (!path.empty()) {
        Debug::Logger::Info("Config", "config path override set to '%s'", path.c_str());
    }
}

std::string ConfigLocator::OverridePath()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_override;
}

const char* ConfigLocator::DefaultFileName() noexcept
{
    return kDefaultFileName;
}

std::string ConfigLocator::Resolve(const std::string& hint)
{
    if (!hint.empty()) {
        return hint;
    }

    const std::string override = OverridePath();
    if (!override.empty()) {
        return override;
    }

    for (const std::filesystem::path& candidate : DefaultCandidates()) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            Debug::Logger::Debug("Config", "resolved config to '%s'", candidate.string().c_str());
            return candidate.string();
        }
    }

    // Nothing found: hand back the bare name so the loader's "not found"
    // message is reported against the working directory, as before.
    return kDefaultFileName;
}

} // namespace Concord
