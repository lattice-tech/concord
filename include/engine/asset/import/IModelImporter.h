#ifndef CONCORD_IMODELIMPORTER_H
#define CONCORD_IMODELIMPORTER_H

#include "engine/asset/import/ImportedModel.h"
#include "engine/asset/import/ImportContext.h"

#include <string>
#include <string_view>

namespace Concord::Asset {

/**
 * Contract for a parser that reads one model file format into an ImportedModel.
 *
 * Each concrete importer (OBJ, glTF, STL, PLY, ...) implements this and
 * registers itself with ModelImporterRegistry, which dispatches by file
 * extension. An importer reports the extensions it owns through
 * SupportsExtension and does all of its work in Import: open the file, decode
 * it, build geometry/materials, and return the result. A failed import returns
 * an ImportedModel with no meshes (HasGeometry == false); the caller decides
 * how to report that.
 *
 * Importers are format-pure: they never touch the graphics API, so they run on
 * any thread and can be unit-tested in isolation. GPU upload is a separate
 * concern handled by the render thread (see Object::Model).
 */
class IModelImporter {
public:
    virtual ~IModelImporter() = default;

    /**
     * True when this importer handles `ext` (compared case-insensitively,
     * without a leading dot, e.g. "obj", "gltf", "glb").
     */
    virtual bool SupportsExtension(std::string_view ext) const = 0;

    /**
     * Reads the canonical source in @p context. Security, budget, malformed
     * input, or allocation failures must produce an empty model; partial
     * geometry must not cross the registry boundary.
     */
    virtual ImportedModel Import(const std::string& path,
                                 ImportContext& context) = 0;
};

} // namespace Concord::Asset

#endif // CONCORD_IMODELIMPORTER_H
