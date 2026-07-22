#ifndef CONCORD_OBJ_MATERIALLIBRARY_H
#define CONCORD_OBJ_MATERIALLIBRARY_H

#include "engine/material/MaterialDesc.h"

#include <string>
#include <unordered_map>

namespace Concord::Asset::Obj {

/** A material parsed from an MTL library, mapped onto Concord's descriptor. */
struct ParsedMaterial {
    std::string name;
    Material::MaterialDesc desc{};
};

/** Name -> parsed material table, as produced by ParseMtl and consumed by Finalize. */
using MaterialTable = std::unordered_map<std::string, ParsedMaterial>;

/**
 * Parses an .mtl file into a name -> material table. Only the fields Concord's
 * material model uses are read; unknown tokens are skipped so esoteric MTLs do
 * not abort the import. Paths in map_* are resolved relative to the MTL file's
 * directory (which is the same as the OBJ's, by convention).
 */
MaterialTable ParseMtl(const std::string& mtlPath);

} // namespace Concord::Asset::Obj

#endif // CONCORD_OBJ_MATERIALLIBRARY_H
