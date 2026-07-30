#ifndef CONCORD_IMPORTPATHSANDBOX_H
#define CONCORD_IMPORTPATHSANDBOX_H

#include <filesystem>
#include <optional>
#include <string_view>

namespace Concord::Asset {

/** @brief Canonical source path and root for resolving untrusted dependencies. */
class ImportPathSandbox {
public:
    static std::optional<ImportPathSandbox> Create(
        const std::filesystem::path& source,
        const std::optional<std::filesystem::path>& allowedRoot = {});

    const std::filesystem::path& SourcePath() const noexcept { return m_source; }
    const std::filesystem::path& RootPath() const noexcept { return m_root; }

    /** Resolves an existing regular dependency below the source directory. */
    std::optional<std::filesystem::path> ResolveDependency(
        std::string_view reference) const;

private:
    ImportPathSandbox(std::filesystem::path source, std::filesystem::path root)
        : m_source(std::move(source)), m_root(std::move(root)) {}

    std::filesystem::path m_source;
    std::filesystem::path m_root;
};

} // namespace Concord::Asset

#endif // CONCORD_IMPORTPATHSANDBOX_H
