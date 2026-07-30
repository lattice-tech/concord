#include "engine/asset/cook/CookSession.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <utility>

namespace Concord::Asset {
namespace {

constexpr std::uint32_t kPassthroughMagic =
    (static_cast<std::uint32_t>('C') << 0)
    | (static_cast<std::uint32_t>('B') << 8)
    | (static_cast<std::uint32_t>('I') << 16)
    | (static_cast<std::uint32_t>('N') << 24);
constexpr std::uint32_t kPassthroughVersion = 1;

std::string JoinPath(std::string_view root, std::string_view relative)
{
    if (root.empty()) {
        return std::string(relative);
    }
    std::string out(root);
    if (out.back() != '/' && out.back() != '\\') {
        out.push_back('/');
    }
    out.append(relative);
    return out;
}

} // namespace

CookSession::CookSession(CookCatalog catalog, CookManifest priorManifest,
                         std::uint32_t cookerVersion, CookStorage storage,
                         CookProduceFn produce, std::string outputRoot)
    : m_catalog(std::move(catalog))
    , m_manifest(std::move(priorManifest))
    , m_cookerVersion(cookerVersion)
    , m_storage(std::move(storage))
    , m_produce(std::move(produce))
    , m_outputRoot(std::move(outputRoot))
{
}

std::string CookSession::OutputRelativePath(const AssetId& id)
{
    const std::string& key = id.Key();
    const std::size_t colon = key.find(':');
    std::string relative = colon == std::string::npos ? key : key.substr(colon + 1);
    const std::size_t slash = relative.find_last_of('/');
    const std::size_t dot = relative.find_last_of('.');
    if (dot != std::string::npos
        && (slash == std::string::npos || dot > slash)
        && relative.find(":guid:") == std::string::npos) {
        relative.resize(dot);
    }
    relative.append(".ccook");
    std::string out(AssetTypeName(id.Type()));
    out.push_back('/');
    out.append(relative);
    return out;
}

std::string CookSession::OutputAbsolutePath(const AssetId& id) const
{
    return JoinPath(m_outputRoot, OutputRelativePath(id));
}

CookSessionResult CookSession::Run()
{
    CookSessionResult result;
    result.manifest = m_manifest;

    if (!m_produce || !m_storage.writeFileAtomic || !m_storage.readFile
        || !m_storage.removeFile || !m_storage.exists) {
        result.error = "cook session is missing produce or storage callbacks";
        return result;
    }

    const std::optional<AssetDependencyGraph> graph = m_catalog.BuildGraph();
    if (!graph) {
        result.error = "catalog could not build a dependency graph";
        return result;
    }

    const std::optional<CookPlan> plan =
        CookPlanner::Plan(*graph, m_manifest, m_cookerVersion);
    if (!plan) {
        result.error = "dependency graph contains a cycle";
        return result;
    }
    result.plan = *plan;

    for (const CookPlanEntry& entry : plan->entries) {
        CookAssetResult assetResult;
        assetResult.id = entry.id;
        assetResult.resolvedHash = entry.resolvedHash;
        assetResult.outputPath = OutputAbsolutePath(entry.id);

        const CookCatalogEntry* catalogEntry = m_catalog.Find(entry.id);
        if (catalogEntry == nullptr) {
            assetResult.status = CookAssetStatus::Failed;
            assetResult.error = "planned asset missing from catalog";
            result.assets.push_back(std::move(assetResult));
            result.error = assetResult.error;
            return result;
        }

        std::string produceError;
        std::optional<std::vector<std::uint8_t>> bytes =
            m_produce(*catalogEntry, entry.resolvedHash, produceError);
        if (!bytes || bytes->empty()) {
            assetResult.status = CookAssetStatus::Failed;
            assetResult.error = produceError.empty() ? "produce returned no bytes"
                                                     : produceError;
            result.assets.push_back(std::move(assetResult));
            result.error = assetResult.error;
            return result;
        }

        std::string writeError;
        if (!m_storage.writeFileAtomic(assetResult.outputPath, bytes->data(),
                                       bytes->size(), writeError)) {
            assetResult.status = CookAssetStatus::Failed;
            assetResult.error = writeError.empty() ? "failed to write cooked output"
                                                   : writeError;
            result.assets.push_back(std::move(assetResult));
            result.error = assetResult.error;
            return result;
        }

        std::vector<std::uint8_t> verified;
        std::string readError;
        if (!m_storage.readFile(assetResult.outputPath, verified, readError)
            || verified != *bytes) {
            assetResult.status = CookAssetStatus::Failed;
            assetResult.error = readError.empty()
                ? "cooked output failed hash verification"
                : readError;
            result.assets.push_back(std::move(assetResult));
            result.error = assetResult.error;
            return result;
        }

        assetResult.outputHash = HashBytes(verified.data(), verified.size());
        if (!CookPlanner::RecordCooked(m_manifest, entry, assetResult.outputHash,
                                       m_cookerVersion)) {
            assetResult.status = CookAssetStatus::Failed;
            assetResult.error = "failed to record cook in manifest";
            result.assets.push_back(std::move(assetResult));
            result.error = assetResult.error;
            return result;
        }

        assetResult.status = CookAssetStatus::Cooked;
        result.assets.push_back(std::move(assetResult));
    }

    CookManifest next;
    for (const CookRecord& record : m_manifest.Records()) {
        if (m_catalog.Contains(record.id)) {
            next.Put(record);
            continue;
        }
        result.pruned.push_back(record.id);
        std::string removeError;
        const std::string path = OutputAbsolutePath(record.id);
        if (m_storage.exists(path) && !m_storage.removeFile(path, removeError)) {
            result.error = removeError.empty() ? "failed to prune cooked output"
                                               : removeError;
            result.manifest = m_manifest;
            return result;
        }
    }
    m_manifest = std::move(next);

    for (const CookCatalogEntry& entry : m_catalog.Entries()) {
        const CookRecord* record = m_manifest.Find(entry.id);
        if (record == nullptr) {
            continue;
        }
        bool planned = false;
        for (const CookAssetResult& cooked : result.assets) {
            if (cooked.id == entry.id) {
                planned = true;
                break;
            }
        }
        if (planned) {
            continue;
        }
        const std::string path = OutputAbsolutePath(entry.id);
        std::vector<std::uint8_t> existing;
        std::string readError;
        if (!m_storage.exists(path)
            || !m_storage.readFile(path, existing, readError)
            || HashBytes(existing.data(), existing.size()) != record->outputHash) {
            result.error = "existing cooked output missing or hash mismatch for "
                + entry.id.Key();
            result.manifest = m_manifest;
            return result;
        }
        CookAssetResult skipped;
        skipped.id = entry.id;
        skipped.status = CookAssetStatus::SkippedUpToDate;
        skipped.resolvedHash = record->resolvedHash;
        skipped.outputHash = record->outputHash;
        skipped.outputPath = path;
        result.assets.push_back(std::move(skipped));
    }

    result.manifest = m_manifest;
    result.ok = true;
    return result;
}

std::vector<std::uint8_t> EncodePassthroughCooked(const CookCatalogEntry& entry,
                                                  AssetContentHash resolvedHash)
{
    Serialization::BinaryWriter writer;
    writer.PutU32(kPassthroughMagic);
    writer.PutU32(kPassthroughVersion);
    writer.PutString(entry.id.Key());
    writer.PutU64(resolvedHash.high);
    writer.PutU64(resolvedHash.low);
    writer.PutBytes(entry.sourceBytes.data(), entry.sourceBytes.size());
    return writer.Take();
}

std::optional<std::vector<std::uint8_t>> DecodePassthroughCooked(
    const std::uint8_t* data, std::size_t size)
{
    Serialization::BinaryReader reader(data, size);
    if (reader.GetU32() != kPassthroughMagic || reader.GetU32() != kPassthroughVersion) {
        return std::nullopt;
    }
    (void)reader.GetString(4096);
    (void)reader.GetU64();
    (void)reader.GetU64();
    std::string payload;
    reader.GetBytes(payload, 64u * 1024u * 1024u);
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

} // namespace Concord::Asset
