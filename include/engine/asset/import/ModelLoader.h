#ifndef CONCORD_MODELLOADER_H
#define CONCORD_MODELLOADER_H

#include "Concord/CExport.h"
#include "engine/asset/import/ImportedModel.h"
#include "engine/asset/import/ImportContext.h"

#include <string>

namespace Concord::Asset {

class CookedModelSource;

/**
 * The high-level entry point for loading a model file.
 *
 * Application code calls `ModelLoader::Import("Assets/models/tree.obj")` and
 * gets back a fully parsed ImportedModel; the registry underneath picks the
 * right importer by extension. This thin facade keeps callers from depending
 * on the registry singleton directly, and gives a single, memorable name to
 * the "load a model" operation.
 */
class CENGINE_API ModelLoader {
public:
    /**
     * Imports `path`, dispatching by extension to the registered importer.
     * @return The parsed model; HasGeometry() is false on any failure.
     */
    static ImportedModel Import(const std::string& path,
                                const ImportOptions& options = {});

    /**
     * Installs (or clears, with nullptr) the cooked package consulted before
     * live import. When set, Import first asks the source for a cooked model
     * under `path` and only falls back to the extension-dispatched importers
     * on a miss. The source must outlive its installation and be swapped from
     * one thread only.
     */
    static void SetCookedSource(const CookedModelSource* source) noexcept;
};

} // namespace Concord::Asset

#endif // CONCORD_MODELLOADER_H
