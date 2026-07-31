#include "engine/asset/runtime/CookedModelSource.h"

#include "engine/asset/cook/CookIo.h"
#include "engine/asset/cook/CookSession.h"
#include "engine/asset/cook/CookedModel.h"
#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"
#include "engine/asset/id/AssetType.h"

#include <utility>
#include <vector>

namespace Concord::Asset {

bool CookedModelSource::Open(const std::string& outputRoot,
                             const std::string& manifestPath,
                             std::string& errorOut)
{
    auto manifest = CookIo::LoadManifest(manifestPath, errorOut);
    if (!manifest) {
        return false;
    }
    if (manifest->Size() == 0 && !CookIo::FileExists(manifestPath)) {
        errorOut = "cook manifest not found: " + manifestPath;
        return false;
    }
    m_root = outputRoot;
    while (!m_root.empty() && (m_root.back() == '/' || m_root.back() == '\\')) {
        m_root.pop_back();
    }
    m_manifest = std::move(*manifest);
    m_open = true;
    return true;
}

void CookedModelSource::Close() noexcept
{
    m_root.clear();
    m_manifest = CookManifest{};
    m_open = false;
}

bool CookedModelSource::TryLoadModel(const std::string& virtualPath,
                                     ImportedModel& out) const
{
    if (!m_open) {
        return false;
    }
    const auto id = AssetId::FromVirtualPath(virtualPath, AssetType::Mesh);
    if (!id) {
        return false;
    }
    const CookRecord* record = m_manifest.Find(*id);
    if (record == nullptr) {
        return false;
    }

    const std::string path =
        m_root + "/" + CookSession::OutputRelativePath(*id);
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!CookIo::ReadFile(path, bytes, error)) {
        return false;
    }
    if (HashBytes(bytes.data(), bytes.size()) != record->outputHash) {
        return false;
    }
    if (!CookedModel::LooksLikeCookedModel(bytes.data(), bytes.size())) {
        return false;
    }

    auto model = CookedModel::Decode(bytes.data(), bytes.size());
    if (!model) {
        return false;
    }
    out = std::move(*model);
    return true;
}

} // namespace Concord::Asset
