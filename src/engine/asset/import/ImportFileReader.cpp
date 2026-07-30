#include "engine/asset/import/ImportFileReader.h"

#include "engine/asset/import/CheckedSize.h"

#include <fstream>
#include <limits>

namespace Concord::Asset {
namespace {

bool FileSize(const std::filesystem::path& path, std::size_t& size) noexcept
{
    std::error_code error;
    const std::uintmax_t rawSize = std::filesystem::file_size(path, error);
    return !error && TryCastSize(rawSize, size);
}

} // namespace

bool ValidatePrimaryFile(ImportContext& context) noexcept
{
    std::size_t size = 0;
    return FileSize(context.Paths().SourcePath(), size)
        && context.Budget().ConsumePrimaryBytes(size);
}

bool ReadDependencyFile(ImportContext& context, std::string_view reference,
                        std::vector<std::uint8_t>& bytes,
                        std::filesystem::path* resolvedPath)
{
    const auto path = context.Paths().ResolveDependency(reference);
    if (!path || !context.Budget().ConsumeDependency()) {
        return false;
    }
    std::size_t size = 0;
    if (!FileSize(*path, size)
        || !context.Budget().ConsumeDependencyBytes(size)
        || size > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    std::ifstream input(*path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::vector<std::uint8_t> loaded(size);
    if (!input.read(reinterpret_cast<char*>(loaded.data()),
                    static_cast<std::streamsize>(size))) {
        return false;
    }
    bytes = std::move(loaded);
    if (resolvedPath) {
        *resolvedPath = *path;
    }
    return true;
}

} // namespace Concord::Asset
