#ifndef CONCORD_COOKSESSION_H
#define CONCORD_COOKSESSION_H

#include "engine/asset/cook/CookCatalog.h"
#include "engine/asset/cook/CookManifest.h"
#include "engine/asset/cook/CookPlanner.h"
#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset {

/** Outcome of a single asset cook attempt. */
enum class CookAssetStatus : std::uint8_t {
    Cooked,
    Failed,
    SkippedUpToDate,
};

/** One finished (or failed) asset within a cook run. */
struct CookAssetResult {
    AssetId id;
    CookAssetStatus status = CookAssetStatus::Failed;
    AssetContentHash resolvedHash{};
    AssetContentHash outputHash{};
    std::string outputPath;
    std::string error;
};

/** Aggregate result of CookSession::Run. */
struct CookSessionResult {
    bool ok = false;
    CookPlan plan;
    std::vector<CookAssetResult> assets;
    std::vector<AssetId> pruned;
    CookManifest manifest;
    std::string error;
};

/**
 * @brief Callable that turns one planned asset into cooked bytes.
 *
 * Receives the catalog entry (source bytes/path + declared deps) and the
 * resolved input hash the cooker must record. On success returns non-empty
 * cooked bytes; on failure returns nullopt and may set `errorOut`.
 */
using CookProduceFn = std::function<std::optional<std::vector<std::uint8_t>>(
    const CookCatalogEntry& entry, AssetContentHash resolvedHash,
    std::string& errorOut)>;

/**
 * @brief Filesystem boundary used by CookSession so unit tests need no disk.
 *
 * Production wiring uses the helpers in CookIo; tests inject an in-memory store.
 */
struct CookStorage {
    std::function<bool(std::string_view path, std::vector<std::uint8_t>& out,
                       std::string& errorOut)>
        readFile;
    std::function<bool(std::string_view path, const std::uint8_t* data,
                       std::size_t size, std::string& errorOut)>
        writeFileAtomic;
    std::function<bool(std::string_view path, std::string& errorOut)> removeFile;
    std::function<bool(std::string_view path)> exists;
};

/**
 * @brief Incremental cook driver over a catalog, prior manifest, and storage.
 *
 * Plans with CookPlanner, invokes the produce callback only for assets that
 * need rebuild, writes byte-identical outputs through atomic storage helpers,
 * re-reads each output to verify the recorded output hash, prunes cooked files
 * whose sources disappeared from the catalog, and returns an updated manifest.
 * Performs no real I/O itself — every path access goes through CookStorage.
 */
class CookSession {
public:
    /**
     * @param catalog Current source set (deterministic order).
     * @param priorManifest Manifest from the previous cook (may be empty).
     * @param cookerVersion Bumped whenever cook output layout or algorithms change.
     * @param storage Injected filesystem operations.
     * @param produce Callback that emits cooked bytes for one planned asset.
     * @param outputRoot Directory prefix for cooked files (no trailing separator).
     */
    CookSession(CookCatalog catalog, CookManifest priorManifest,
                std::uint32_t cookerVersion, CookStorage storage,
                CookProduceFn produce, std::string outputRoot);

    /**
     * Relative cooked path for `id` under the package: type/path with the
     * source extension replaced by ".ccook" (or "<key>.ccook" for GUID ids).
     */
    static std::string OutputRelativePath(const AssetId& id);

    /** Joins outputRoot and OutputRelativePath with a single '/'. */
    std::string OutputAbsolutePath(const AssetId& id) const;

    /**
     * Runs one incremental cook. On graph cycle, missing dependency, storage
     * failure, or produce failure, returns ok=false and leaves prior outputs
     * that were not successfully rewritten untouched.
     */
    CookSessionResult Run();

private:
    CookCatalog m_catalog;
    CookManifest m_manifest;
    std::uint32_t m_cookerVersion = 0;
    CookStorage m_storage;
    CookProduceFn m_produce;
    std::string m_outputRoot;
};

/** Default-produce path: copy length-prefixed source bytes into a CBIN v1 blob. */
std::vector<std::uint8_t> EncodePassthroughCooked(
    const CookCatalogEntry& entry, AssetContentHash resolvedHash);

/** Decodes a CBIN v1 blob written by EncodePassthroughCooked; nullopt on error. */
std::optional<std::vector<std::uint8_t>> DecodePassthroughCooked(
    const std::uint8_t* data, std::size_t size);

} // namespace Concord::Asset

#endif // CONCORD_COOKSESSION_H
