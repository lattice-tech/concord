#ifndef CONCORD_PLY_MESHBUILDER_H
#define CONCORD_PLY_MESHBUILDER_H

#include "engine/asset/import/ImportedModel.h"
#include "engine/asset/import/ply/PlyHeader.h"

#include <istream>
#include <string>

namespace Concord::Asset::Ply {

/**
 * Reads the PLY data section (stream positioned after the header) into `model`:
 * decodes the vertex and face elements, generates flat normals when the file
 * has none, assembles a single sub-mesh (16- or 32-bit indices) and computes
 * the model bounds. Returns false without appending geometry on malformed
 * data or an import-limit violation.
 */
bool BuildMesh(std::istream& file, const PlyHeader& header, const std::string& path, ImportedModel& model);

} // namespace Concord::Asset::Ply

#endif // CONCORD_PLY_MESHBUILDER_H
