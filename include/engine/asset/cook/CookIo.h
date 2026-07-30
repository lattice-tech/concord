#ifndef CONCORD_COOKIO_H
#define CONCORD_COOKIO_H

#include "engine/asset/cook/CookManifest.h"
#include "engine/asset/cook/CookSession.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset {

/**
 * @brief Bounded filesystem helpers for the offline cooker.
 *
 * Every path is treated as an opaque OS path string. Reads refuse files larger
 * than the supplied budget. Writes create parent directories, stream to a
 * sibling temporary file, flush, then rename over the destination so a crash
 * never leaves a half-written cooked blob at the final path.
 */
namespace CookIo {

/** Maximum bytes accepted by ReadFile when the caller does not pass a budget. */
inline constexpr std::size_t kDefaultMaxFileBytes = 256ull * 1024ull * 1024ull;

/**
 * Reads the entire file at `path` into `out` when it is a regular file not
 * larger than `maxBytes`. Returns false and sets `errorOut` on any failure.
 */
bool ReadFile(std::string_view path, std::vector<std::uint8_t>& out,
              std::string& errorOut, std::size_t maxBytes = kDefaultMaxFileBytes);

/**
 * Atomically replaces `path` with `data` via temp+rename. Creates missing parent
 * directories. Returns false and sets `errorOut` on failure (destination is
 * left unchanged when the rename never runs).
 */
bool WriteFileAtomic(std::string_view path, const std::uint8_t* data,
                     std::size_t size, std::string& errorOut);

/** Removes `path` if it exists. Missing files are success. */
bool RemoveFile(std::string_view path, std::string& errorOut);

/** True when `path` names an existing regular file. */
bool FileExists(std::string_view path);

/** Loads a text cook manifest; empty optional on I/O error, empty manifest if missing. */
std::optional<CookManifest> LoadManifest(std::string_view path,
                                         std::string& errorOut,
                                         std::size_t maxBytes = 16ull * 1024ull * 1024ull);

/** Atomically writes `manifest.Serialize()` to `path`. */
bool SaveManifest(std::string_view path, const CookManifest& manifest,
                  std::string& errorOut);

/** Builds a CookStorage bound to the real filesystem helpers above. */
CookStorage MakeFilesystemStorage();

} // namespace CookIo

} // namespace Concord::Asset

#endif // CONCORD_COOKIO_H
