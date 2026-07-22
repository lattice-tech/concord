#ifndef CONCORD_IMPORTPATHS_H
#define CONCORD_IMPORTPATHS_H

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace Concord::Asset::Paths {

/**
 * The lowercased extension of `path` without a leading dot, e.g. "obj" for
 * "Assets/models/Tree.OBJ". Returns an empty string when there is none.
 */
inline std::string Extension(std::string_view path)
{
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) {
        return {};
    }
    std::string ext(path.substr(dot + 1));
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

/**
 * The directory portion of `path` up to and including the last separator, or
 * an empty string when `path` has no directory. Used to resolve sibling files
 * an importer references (MTL textures, glTF buffers) relative to the model.
 */
inline std::string Directory(std::string_view path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash + 1));
}

/**
 * Concatenates `dir` and `name`, inserting a forward slash when `dir` has no
 * trailing separator. It does not normalize or validate `name`; use
 * ResolveWithin() for references from untrusted files.
 */
inline std::string Join(std::string_view dir, std::string_view name)
{
    if (dir.empty()) {
        return std::string(name);
    }
    std::string out(dir);
    if (out.back() != '/' && out.back() != '\\') {
        out.push_back('/');
    }
    out.append(name);
    return out;
}

namespace Detail {

inline bool HasPathPrefix(const std::filesystem::path& path,
                          const std::filesystem::path& prefix)
{
    auto pathIt = path.begin();
    for (auto prefixIt = prefix.begin(); prefixIt != prefix.end(); ++prefixIt, ++pathIt) {
        if (pathIt == path.end() || *pathIt != *prefixIt) {
            return false;
        }
    }
    return true;
}

} // namespace Detail

/**
 * Resolves an untrusted relative path beneath `dir`.
 *
 * Absolute paths and paths that resolve outside `dir`, including through an
 * existing symbolic link, are rejected. Returns an empty string on rejection
 * or when the filesystem cannot resolve the path.
 */
inline std::string ResolveWithin(std::string_view dir, std::string_view name)
{
    try {
        const std::filesystem::path relative(name);
        if (relative.empty() || relative.has_root_path()) {
            return {};
        }

        std::error_code error;
        const std::filesystem::path rootInput = dir.empty()
            ? std::filesystem::path(".")
            : std::filesystem::path(dir);
        std::filesystem::path root = std::filesystem::absolute(rootInput, error);
        if (error) {
            return {};
        }
        root = std::filesystem::weakly_canonical(root, error);
        if (error) {
            return {};
        }

        const std::filesystem::path resolved =
            std::filesystem::weakly_canonical(root / relative, error);
        if (error || !Detail::HasPathPrefix(resolved, root)) {
            return {};
        }
        return resolved.string();
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }
}

} // namespace Concord::Asset::Paths

#endif // CONCORD_IMPORTPATHS_H
