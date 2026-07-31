#include "engine/asset/cook/ModelCookProducer.h"

#include "engine/asset/cook/CookedModel.h"
#include "engine/asset/id/AssetType.h"
#include "engine/asset/import/ModelLoader.h"

namespace Concord::Asset {

CookProduceFn MakeModelCookProduce()
{
    return [](const CookCatalogEntry& entry, AssetContentHash resolvedHash,
              std::string& errorOut) -> std::optional<std::vector<std::uint8_t>> {
        if (entry.id.Type() == AssetType::Mesh && !entry.sourcePath.empty()) {
            const ImportedModel model = ModelLoader::Import(entry.sourcePath);
            if (!model.HasGeometry()) {
                errorOut = "model import produced no geometry: "
                    + entry.sourcePath;
                return std::nullopt;
            }
            if (!model.IsSkinned()) {
                std::vector<std::uint8_t> cooked = CookedModel::Encode(model);
                if (!cooked.empty()) {
                    return cooked;
                }
                errorOut = "cooked model encoding failed: " + entry.sourcePath;
                return std::nullopt;
            }
            // Skinned models are not representable in CookedModel yet; ship
            // the source bytes so the runtime falls back to live import.
        }
        return EncodePassthroughCooked(entry, resolvedHash);
    };
}

} // namespace Concord::Asset
