#include "engine/scene/io/SceneIOFile.h"

#include "engine/scene/io/SceneIOCodec.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Concord::Detail::SceneIo {
namespace {

std::atomic<std::uint64_t> g_temporarySequence{1};

std::filesystem::path TemporaryPath(const std::filesystem::path& target)
{
    const std::uint64_t sequence = g_temporarySequence.fetch_add(
        1, std::memory_order_relaxed);
    return target.parent_path()
        / (target.filename().string() + ".tmp." + std::to_string(sequence));
}

bool ReplaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& target)
{
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

} // namespace

bool ReadSceneFile(const std::string& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamsize>(kMaxSceneFileBytes)) {
        return false;
    }
    input.seekg(0);
    bytes.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(
        input.read(reinterpret_cast<char*>(bytes.data()), size));
}

bool WriteSceneFileAtomic(const std::string& path,
                          const std::vector<std::uint8_t>& bytes)
{
    const std::filesystem::path target(path);
    std::error_code error;
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    const std::filesystem::path temporary = TemporaryPath(target);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    if (ReplaceFile(temporary, target)) {
        return true;
    }
    std::filesystem::remove(temporary, error);
    return false;
}

} // namespace Concord::Detail::SceneIo
