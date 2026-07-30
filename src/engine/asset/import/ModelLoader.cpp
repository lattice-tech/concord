#include "engine/asset/import/ModelLoader.h"

#include "engine/asset/import/ModelImporterRegistry.h"

namespace Concord::Asset {

ImportedModel ModelLoader::Import(const std::string& path,
                                  const ImportOptions& options)
{
    return ModelImporterRegistry::Instance().Import(path, options);
}

} // namespace Concord::Asset
