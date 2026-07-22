#include "engine/app/GameConfigLoader.h"

#include "engine/debug/Logger.h"
#include "engine/env/EnvStore.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {

/** Strips leading/trailing ASCII whitespace. */
std::string Trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

/** Removes one layer of matching surrounding quotes, if present. */
std::string Unquote(const std::string& value)
{
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')
        && value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

/** ASCII-lowercased copy, for case-insensitive keyword matching. */
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/**
 * Reports a known key whose value could not be recognized, and returns the
 * default that will be used instead (Requirement 7.2). Kept in one place so
 * every config key reports unrecognized values the same way.
 */
template <typename T>
T UnknownValue(const char* key, const std::string& value, T fallback, const char* applied)
{
    Concord::Debug::Logger::Warn("Config", "unrecognized %s value '%s'; using %s",
                                 key, value.c_str(), applied);
    return fallback;
}

/** Maps a formal API name to its enum. Concord is Vulkan-only. */
Concord::RenderBackendType ParseRenderBackendType(const std::string& value,
                                                  Concord::RenderBackendType fallback)
{
    const std::string lowered = ToLower(value);
    if (lowered == "vulkan" || lowered == "auto" || lowered == "default") {
        return Concord::RenderBackendType::Vulkan;
    }
    // Legacy DirectX* names: refuse silently with a diagnostic and keep Vulkan.
    if (lowered == "directx12" || lowered == "direct3d12" || lowered == "dx12"
        || lowered == "directx11" || lowered == "direct3d11" || lowered == "dx11"
        || lowered == "opengl" || lowered == "gl") {
        Concord::Debug::Logger::Warn(
            "Config",
            "renderBackend='%s' is not supported (engine is Vulkan-only); using Vulkan",
            value.c_str());
        return Concord::RenderBackendType::Vulkan;
    }
    return UnknownValue("renderBackend", value, fallback, "Vulkan");
}

/** Maps a runtime-mode name (case-insensitive) to its enum. */
Concord::RuntimeMode ParseRuntimeMode(const std::string& value, Concord::RuntimeMode fallback)
{
    const std::string lowered = ToLower(value);
    if (lowered == "debug") {
        return Concord::RuntimeMode::Debug;
    }
    if (lowered == "release") {
        return Concord::RuntimeMode::Release;
    }
    return UnknownValue("mode", value, fallback, fallback == Concord::RuntimeMode::Debug ? "Debug" : "Release");
}

/** Maps an anti-aliasing name (case-insensitive, with aliases) to its enum. */
Concord::AntiAliasing ParseAntiAliasing(const std::string& value, Concord::AntiAliasing fallback)
{
    const std::string lowered = ToLower(value);
    if (lowered == "off" || lowered == "none") {
        return Concord::AntiAliasing::Off;
    }
    if (lowered == "msaa2" || lowered == "msaa2x" || lowered == "2x") {
        return Concord::AntiAliasing::Msaa2;
    }
    if (lowered == "msaa4" || lowered == "msaa4x" || lowered == "4x" || lowered == "msaa") {
        return Concord::AntiAliasing::Msaa4;
    }
    if (lowered == "msaa8" || lowered == "msaa8x" || lowered == "8x") {
        return Concord::AntiAliasing::Msaa8;
    }
    if (lowered == "fxaa") {
        return Concord::AntiAliasing::Fxaa;
    }
    if (lowered == "smaa2" || lowered == "smaa2x" || lowered == "smaa") {
        return Concord::AntiAliasing::Smaa2;
    }
    if (lowered == "smaa4" || lowered == "smaa4x") {
        return Concord::AntiAliasing::Smaa4;
    }
    return UnknownValue("antialiasing", value, fallback, ToString(fallback));
}

} // namespace

namespace Concord {

GameConfig GameConfigLoader::LoadFromFile(const std::string& path, const GameConfig& fallback)
{
    // A load fully defines the global environment, so start from a clean
    // slate even when the file is missing (yielding zero env variables).
    EnvStore::Clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        Debug::Logger::Warn("Config", "%s not found, using defaults", path.c_str());
        Debug::Logger::ConfigureForDebug(fallback.mode == RuntimeMode::Debug);
        return fallback;
    }

    GameConfig config = fallback;
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        const auto separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Unquote(Trim(trimmed.substr(separator + 1)));

        if (key == "renderBackend") {
            config.renderBackend = ParseRenderBackendType(value, config.renderBackend);
        } else if (key == "mode") {
            config.mode = ParseRuntimeMode(value, config.mode);
        } else if (key == "antialiasing" || key == "aa") {
            config.antialiasing = ParseAntiAliasing(value, config.antialiasing);
        } else if (key.size() > 3 && key.starts_with("env")) {
            // `envVersion` -> global variable `Version`, read via Env("Version").
            EnvStore::Set(key.substr(3), value);
        }
        // Any other key is intentionally ignored (forward-compatible format).
        // `icon` in particular is read by the root CMakeLists.txt at CMake
        // configure time (see docs/配置.md): a built exe's icon is a
        // resource baked in at build time, so GameConfig has no runtime
        // field for it and this loader never needs to see that key.
    }

    // Apply the parsed mode to the logger before anyone else logs, so
    // `mode=Debug` immediately unlocks Debug-level diagnostics engine-wide.
    Debug::Logger::ConfigureForDebug(config.mode == RuntimeMode::Debug);
    Debug::Logger::Info("Config", "loaded %s (renderBackend=%s, mode=%s, aa=%s)",
                        path.c_str(), ToString(config.renderBackend), ToString(config.mode),
                        ToString(config.antialiasing));
    return config;
}

} // namespace Concord
