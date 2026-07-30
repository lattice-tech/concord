#include "engine/asset/import/ImportPathSandbox.h"

#include <string>

namespace Concord::Asset {
namespace {

bool HasPrefix(const std::filesystem::path& path,
               const std::filesystem::path& prefix)
{
    auto pathIt = path.begin();
    for (auto prefixIt = prefix.begin(); prefixIt != prefix.end();
         ++prefixIt, ++pathIt) {
        if (pathIt == path.end() || *pathIt != *prefixIt) {
            return false;
        }
    }
    return true;
}

bool IsUnsafeReference(std::string_view reference)
{
    if (reference.empty() || reference.find('\0') != std::string_view::npos
        || reference.find('?') != std::string_view::npos
        || reference.find('#') != std::string_view::npos) {
        return true;
    }
    const std::size_t colon = reference.find(':');
    const std::size_t separator = reference.find_first_of("/\\");
    return colon != std::string_view::npos
        && (separator == std::string_view::npos || colon < separator);
}

} // namespace

std::optional<ImportPathSandbox> ImportPathSandbox::Create(
    const std::filesystem::path& source,
    const std::optional<std::filesystem::path>& allowedRoot)
{
    try {
        std::error_code error;
        std::filesystem::path canonicalSource = std::filesystem::canonical(source, error);
        if (error || !std::filesystem::is_regular_file(canonicalSource, error)
            || error) {
            return std::nullopt;
        }
        std::filesystem::path root = canonicalSource.parent_path();
        if (allowedRoot) {
            const std::filesystem::path canonicalAllowed =
                std::filesystem::canonical(*allowedRoot, error);
            if (error || !HasPrefix(canonicalSource, canonicalAllowed)) {
                return std::nullopt;
            }
        }
        return ImportPathSandbox(std::move(canonicalSource), std::move(root));
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> ImportPathSandbox::ResolveDependency(
    std::string_view reference) const
{
    if (IsUnsafeReference(reference)) {
        return std::nullopt;
    }
    try {
        const std::filesystem::path relative{std::string(reference)};
        if (relative.empty() || relative.has_root_path()) {
            return std::nullopt;
        }
        std::error_code error;
        const std::filesystem::path candidate = std::filesystem::canonical(
            m_root / relative, error);
        if (error || !HasPrefix(candidate, m_root)
            || !std::filesystem::is_regular_file(candidate, error) || error) {
            return std::nullopt;
        }
        return candidate;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

} // namespace Concord::Asset
