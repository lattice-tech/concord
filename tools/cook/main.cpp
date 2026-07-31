#include "engine/asset/cook/CookCatalog.h"
#include "engine/asset/cook/CookIo.h"
#include "engine/asset/cook/CookSession.h"
#include "engine/asset/cook/ModelCookProducer.h"
#include "engine/asset/cook/TextureCookProducer.h"

#include "BimgTextureDecode.h"
#include "engine/asset/id/AssetId.h"
#include "engine/asset/id/AssetType.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Concord::Asset::AssetId;
using Concord::Asset::AssetType;
using Concord::Asset::AssetTypeFromName;
using Concord::Asset::CookCatalog;
using Concord::Asset::CookCatalogEntry;
using Concord::Asset::CookIo::LoadManifest;
using Concord::Asset::CookIo::MakeFilesystemStorage;
using Concord::Asset::CookIo::ReadFile;
using Concord::Asset::CookIo::SaveManifest;
using Concord::Asset::CookSession;
using Concord::Asset::MakeBimgTextureDecode;
using Concord::Asset::MakeModelCookProduce;
using Concord::Asset::MakeTextureCookProduce;

constexpr std::uint32_t kCookerVersion = 1;

void PrintUsage()
{
    std::fprintf(stderr,
                 "usage: concord-cook --out <dir> --manifest <path> "
                 "[--version N] <catalog.tsv>\n"
                 "\n"
                 "catalog.tsv columns (TAB-separated), one asset per line:\n"
                 "  type  virtualPath  sourcePath  [depKey ...]\n"
                 "Dependencies are identity keys (e.g. material:models/tree.mat).\n"
                 "Lines starting with # are ignored. Scan order is sorted by key.\n");
}

AssetType ParseType(std::string_view token)
{
    return AssetTypeFromName(token);
}

std::vector<std::string_view> SplitTabs(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t cursor = 0;
    while (cursor <= line.size()) {
        const std::size_t tab = line.find('\t', cursor);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(cursor));
            break;
        }
        fields.push_back(line.substr(cursor, tab - cursor));
        cursor = tab + 1;
    }
    return fields;
}

bool LoadCatalogFile(const std::filesystem::path& path, CookCatalog& catalog,
                     std::string& errorOut)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path.string(), bytes, errorOut)) {
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    std::size_t pos = 0;
    std::size_t lineNo = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end < text.size() ? end + 1 : text.size();
        ++lineNo;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() < 3) {
            errorOut = "catalog line " + std::to_string(lineNo)
                + ": expected type, virtualPath, sourcePath";
            return false;
        }
        const AssetType type = ParseType(fields[0]);
        if (type == AssetType::Unknown) {
            errorOut = "catalog line " + std::to_string(lineNo) + ": unknown type";
            return false;
        }
        const auto id = AssetId::FromVirtualPath(fields[1], type);
        if (!id) {
            errorOut = "catalog line " + std::to_string(lineNo) + ": bad virtual path";
            return false;
        }

        CookCatalogEntry entry;
        entry.id = *id;
        entry.sourcePath = std::string(fields[2]);
        std::vector<std::uint8_t> sourceBytes;
        std::string readError;
        if (!ReadFile(entry.sourcePath, sourceBytes, readError)) {
            errorOut = "catalog line " + std::to_string(lineNo) + ": " + readError;
            return false;
        }
        entry.sourceBytes.assign(sourceBytes.begin(), sourceBytes.end());
        for (std::size_t i = 3; i < fields.size(); ++i) {
            if (fields[i].empty()) {
                continue;
            }
            const auto dep = AssetId::FromKey(fields[i]);
            if (!dep) {
                errorOut = "catalog line " + std::to_string(lineNo)
                    + ": bad dependency key";
                return false;
            }
            entry.dependencies.push_back(*dep);
        }
        if (!catalog.Put(std::move(entry))) {
            errorOut = "catalog line " + std::to_string(lineNo)
                + ": rejected by catalog budgets or invalid fields";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string outDir;
    std::string manifestPath;
    std::string catalogPath;
    std::uint32_t version = kCookerVersion;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
            continue;
        }
        if (arg == "--manifest" && i + 1 < argc) {
            manifestPath = argv[++i];
            continue;
        }
        if (arg == "--version" && i + 1 < argc) {
            version = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            continue;
        }
        if (arg.starts_with("-")) {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            PrintUsage();
            return 2;
        }
        catalogPath = argv[i];
    }

    if (outDir.empty() || manifestPath.empty() || catalogPath.empty()) {
        PrintUsage();
        return 2;
    }

    CookCatalog catalog;
    std::string error;
    if (!LoadCatalogFile(catalogPath, catalog, error)) {
        std::fprintf(stderr, "concord-cook: %s\n", error.c_str());
        return 1;
    }

    auto prior = LoadManifest(manifestPath, error);
    if (!prior) {
        std::fprintf(stderr, "concord-cook: %s\n", error.c_str());
        return 1;
    }

    // Texture assets decode and bake a full mip chain to CookedTexture; mesh
    // assets import and bake to CookedModel; everything else (and skinned
    // models, for now) ships as a passthrough blob.
    CookSession session(std::move(catalog), std::move(*prior), version,
                        MakeFilesystemStorage(),
                        MakeTextureCookProduce(MakeBimgTextureDecode(),
                                               MakeModelCookProduce()),
                        outDir);
    const auto result = session.Run();
    if (!result.ok) {
        std::fprintf(stderr, "concord-cook: %s\n", result.error.c_str());
        return 1;
    }

    if (!SaveManifest(manifestPath, result.manifest, error)) {
        std::fprintf(stderr, "concord-cook: failed to write manifest: %s\n",
                     error.c_str());
        return 1;
    }

    std::size_t cooked = 0;
    std::size_t skipped = 0;
    for (const auto& asset : result.assets) {
        if (asset.status == Concord::Asset::CookAssetStatus::Cooked) {
            ++cooked;
        } else if (asset.status == Concord::Asset::CookAssetStatus::SkippedUpToDate) {
            ++skipped;
        }
    }
    std::fprintf(stdout,
                 "concord-cook: cooked=%zu skipped=%zu pruned=%zu manifest=%s\n",
                 cooked, skipped, result.pruned.size(), manifestPath.c_str());
    return 0;
}
