#include "engine/asset/cook/CookIo.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace Concord::Asset::CookIo {
namespace {

namespace fs = std::filesystem;

std::string PathString(std::string_view path)
{
    return std::string(path);
}

} // namespace

bool ReadFile(std::string_view path, std::vector<std::uint8_t>& out,
              std::string& errorOut, std::size_t maxBytes)
{
    out.clear();
    std::error_code ec;
    const fs::path filePath(PathString(path));
    if (!fs::is_regular_file(filePath, ec) || ec) {
        errorOut = "not a regular file: " + PathString(path);
        return false;
    }
    const auto size = fs::file_size(filePath, ec);
    if (ec) {
        errorOut = "failed to stat: " + PathString(path);
        return false;
    }
    if (size > maxBytes) {
        errorOut = "file exceeds budget: " + PathString(path);
        return false;
    }
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        errorOut = "failed to open: " + PathString(path);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    if (size > 0
        && !input.read(reinterpret_cast<char*>(out.data()),
                       static_cast<std::streamsize>(size))) {
        out.clear();
        errorOut = "failed to read: " + PathString(path);
        return false;
    }
    return true;
}

bool WriteFileAtomic(std::string_view path, const std::uint8_t* data,
                     std::size_t size, std::string& errorOut)
{
    std::error_code ec;
    const fs::path destination(PathString(path));
    const fs::path parent = destination.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        if (!fs::create_directories(parent, ec) || ec) {
            errorOut = "failed to create directories for: " + PathString(path);
            return false;
        }
    }

    const fs::path temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            errorOut = "failed to open temp file for: " + PathString(path);
            return false;
        }
        if (size > 0
            && !output.write(reinterpret_cast<const char*>(data),
                             static_cast<std::streamsize>(size))) {
            errorOut = "failed to write temp file for: " + PathString(path);
            return false;
        }
        output.flush();
        if (!output) {
            errorOut = "failed to flush temp file for: " + PathString(path);
            return false;
        }
    }

    fs::rename(temporary, destination, ec);
    if (ec) {
        fs::remove(destination, ec);
        ec.clear();
        fs::rename(temporary, destination, ec);
        if (ec) {
            fs::remove(temporary, ec);
            errorOut = "failed to commit: " + PathString(path);
            return false;
        }
    }
    return true;
}

bool RemoveFile(std::string_view path, std::string& errorOut)
{
    std::error_code ec;
    const fs::path filePath(PathString(path));
    if (!fs::exists(filePath, ec)) {
        return true;
    }
    if (!fs::remove(filePath, ec) || ec) {
        errorOut = "failed to remove: " + PathString(path);
        return false;
    }
    return true;
}

bool FileExists(std::string_view path)
{
    std::error_code ec;
    return fs::is_regular_file(fs::path(PathString(path)), ec) && !ec;
}

std::optional<CookManifest> LoadManifest(std::string_view path, std::string& errorOut,
                                         std::size_t maxBytes)
{
    if (!FileExists(path)) {
        return CookManifest{};
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, bytes, errorOut, maxBytes)) {
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    auto manifest = CookManifest::Deserialize(text);
    if (!manifest) {
        errorOut = "malformed cook manifest: " + PathString(path);
        return std::nullopt;
    }
    return manifest;
}

bool SaveManifest(std::string_view path, const CookManifest& manifest,
                  std::string& errorOut)
{
    const std::string text = manifest.Serialize();
    return WriteFileAtomic(path, reinterpret_cast<const std::uint8_t*>(text.data()),
                           text.size(), errorOut);
}

CookStorage MakeFilesystemStorage()
{
    CookStorage storage;
    storage.readFile = [](std::string_view path, std::vector<std::uint8_t>& out,
                          std::string& errorOut) {
        return ReadFile(path, out, errorOut);
    };
    storage.writeFileAtomic = [](std::string_view path, const std::uint8_t* data,
                                 std::size_t size, std::string& errorOut) {
        return WriteFileAtomic(path, data, size, errorOut);
    };
    storage.removeFile = [](std::string_view path, std::string& errorOut) {
        return RemoveFile(path, errorOut);
    };
    storage.exists = [](std::string_view path) { return FileExists(path); };
    return storage;
}

} // namespace Concord::Asset::CookIo
