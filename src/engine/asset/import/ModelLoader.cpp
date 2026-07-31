#include "engine/asset/import/ModelLoader.h"

#include "engine/asset/import/ModelImporterRegistry.h"
#include "engine/asset/runtime/CookedModelSource.h"

#include <atomic>

namespace Concord::Asset {

namespace {

std::atomic<const CookedModelSource*> s_cookedSource{nullptr};

} // namespace

ImportedModel ModelLoader::Import(const std::string& path,
                                  const ImportOptions& options)
{
    if (const CookedModelSource* cooked =
            s_cookedSource.load(std::memory_order_acquire)) {
        ImportedModel model;
        if (cooked->TryLoadModel(path, model)) {
            return model;
        }
    }
    return ModelImporterRegistry::Instance().Import(path, options);
}

void ModelLoader::SetCookedSource(const CookedModelSource* source) noexcept
{
    s_cookedSource.store(source, std::memory_order_release);
}

} // namespace Concord::Asset
