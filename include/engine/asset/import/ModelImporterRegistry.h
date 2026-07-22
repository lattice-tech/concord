#ifndef CONCORD_MODELIMPORTERREGISTRY_H
#define CONCORD_MODELIMPORTERREGISTRY_H

#include "engine/asset/import/ImportedModel.h"

#include <memory>
#include <string>

namespace Concord::Asset {

class IModelImporter;

/**
 * The process-wide table that maps a file extension to the importer that
 * handles it.
 *
 * Built-in importers (OBJ, glTF/GLB, STL, PLY) register themselves at first
 * use, so an application just calls ModelLoader::Import with any supported
 * path. A future format (FBX via Assimp, USD, ...) plugs in the same way:
 * implement IModelImporter and call Register once. The registry owns its
 * importers for the life of the process; they are stateless beyond their
 * parse logic, so sharing a single instance across threads is safe.
 */
class ModelImporterRegistry {
public:
    /** The single registry instance, lazily seeding the built-in importers. */
    static ModelImporterRegistry& Instance();

    /** Takes ownership of `importer` and consults it for future imports. */
    void Register(std::unique_ptr<IModelImporter> importer);

    /**
     * Finds the importer whose SupportsExtension matches `path`'s extension and
     * runs it. Returns an empty model when no importer matches or the import
     * fails, so callers always get a well-formed result to check.
     */
    ImportedModel Import(const std::string& path);

private:
    ModelImporterRegistry();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Concord::Asset

#endif // CONCORD_MODELIMPORTERREGISTRY_H
