#include "engine/asset/import/ModelImporterRegistry.h"

#include "engine/asset/import/DaeImporter.h"
#include "engine/asset/import/GltfImporter.h"
#include "engine/asset/import/IModelImporter.h"
#include "engine/asset/import/ObjImporter.h"
#include "engine/asset/import/PlyImporter.h"
#include "engine/asset/import/StlImporter.h"
#include "engine/asset/import/ThreeDsImporter.h"
#include "engine/asset/import/ImportPaths.h"
#include "engine/asset/import/ImportFileReader.h"
#include "engine/asset/import/ImportModelValidator.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace Concord::Asset {

struct ModelImporterRegistry::Impl {
    std::vector<std::unique_ptr<IModelImporter>> importers;
    bool seeded = false;
    std::shared_mutex mutex;

    void EnsureSeeded()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        if (seeded) {
            return;
        }
        seeded = true;
        importers.push_back(std::make_unique<ObjImporter>());
        importers.push_back(std::make_unique<GltfImporter>());
        importers.push_back(std::make_unique<DaeImporter>());
        importers.push_back(std::make_unique<ThreeDsImporter>());
        importers.push_back(std::make_unique<StlImporter>());
        importers.push_back(std::make_unique<PlyImporter>());
    }
};

ModelImporterRegistry& ModelImporterRegistry::Instance()
{
    static ModelImporterRegistry instance;
    return instance;
}

ModelImporterRegistry::ModelImporterRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

void ModelImporterRegistry::Register(std::unique_ptr<IModelImporter> importer)
{
    m_impl->EnsureSeeded();
    if (importer) {
        std::unique_lock<std::shared_mutex> lock(m_impl->mutex);
        m_impl->importers.push_back(std::move(importer));
    }
}

ImportedModel ModelImporterRegistry::Import(const std::string& path,
                                            const ImportOptions& options)
{
    m_impl->EnsureSeeded();
    const std::string ext = Paths::Extension(path);
    if (ext.empty()) {
        Debug::Logger::Warn("Asset", "import failed: no file extension in '%s'", path.c_str());
        return {};
    }
    IModelImporter* selected = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_impl->mutex);
        for (const std::unique_ptr<IModelImporter>& importer : m_impl->importers) {
            if (importer->SupportsExtension(ext)) {
                selected = importer.get();
                break;
            }
        }
    }
    if (selected != nullptr) {
        const auto sandbox = ImportPathSandbox::Create(path, options.allowedRoot);
        if (!sandbox) {
            Debug::Logger::Warn("Asset", "import source is outside the allowed root or not a file: '%s'",
                                path.c_str());
            return {};
        }
        ImportContext context{ImportBudget(options.limits), *sandbox};
        if (!ValidatePrimaryFile(context)) {
            Debug::Logger::Warn("Asset", "import source exceeds its byte budget: '%s'",
                                path.c_str());
            return {};
        }
        ImportedModel model;
        try {
            model = selected->Import(sandbox->SourcePath().string(), context);
        } catch (const std::bad_alloc&) {
            Debug::Logger::Error("Asset", "import allocation failed for '%s'", path.c_str());
            return {};
        } catch (const std::exception& exception) {
            Debug::Logger::Error("Asset", "import failed for '%s' (%s)",
                                 path.c_str(), exception.what());
            return {};
        } catch (...) {
            Debug::Logger::Error("Asset", "import failed for '%s'", path.c_str());
            return {};
        }
        if (model.HasGeometry()
            && !ValidateImportedModel(model, context.Budget())) {
            Debug::Logger::Warn("Asset", "imported geometry exceeds budget or is malformed: '%s'",
                                path.c_str());
            return {};
        }
        if (!model.HasGeometry()) {
            Debug::Logger::Warn("Asset", "importer produced no geometry from '%s'", path.c_str());
        } else {
            Debug::Logger::Info("Asset", "imported '%s': %zu sub-mesh(es)",
                                path.c_str(), model.meshes.size());
        }
        return model;
    }
    Debug::Logger::Warn("Asset", "no importer registered for '.%s' ('%s')", ext.c_str(), path.c_str());
    return {};
}

} // namespace Concord::Asset
