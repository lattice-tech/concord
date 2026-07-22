#include "engine/asset/import/ModelLoader.h"

#include "engine/asset/import/ModelImporterRegistry.h"

namespace Concord::Asset {

ImportedModel ModelLoader::Import(const std::string& path)
{
    return ModelImporterRegistry::Instance().Import(path);
}

} // namespace Concord::Asset
