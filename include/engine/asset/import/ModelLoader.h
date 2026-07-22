#ifndef CONCORD_MODELLOADER_H
#define CONCORD_MODELLOADER_H

#include "engine/asset/import/ImportedModel.h"

#include <string>

namespace Concord::Asset {

/**
 * The high-level entry point for loading a model file.
 *
 * Application code calls `ModelLoader::Import("Assets/models/tree.obj")` and
 * gets back a fully parsed ImportedModel; the registry underneath picks the
 * right importer by extension. This thin facade keeps callers from depending
 * on the registry singleton directly, and gives a single, memorable name to
 * the "load a model" operation.
 */
class ModelLoader {
public:
    /**
     * Imports `path`, dispatching by extension to the registered importer.
     * @return The parsed model; HasGeometry() is false on any failure.
     */
    static ImportedModel Import(const std::string& path);
};

} // namespace Concord::Asset

#endif // CONCORD_MODELLOADER_H
