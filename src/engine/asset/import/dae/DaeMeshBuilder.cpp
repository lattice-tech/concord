#include "engine/asset/import/dae/DaeMeshBuilder.h"

#include "engine/asset/import/dae/DaePrimitiveBuilder.h"
#include "engine/asset/import/dae/DaeSourceTable.h"

#include <utility>
#include <vector>

namespace Concord::Asset::Dae {

std::vector<DaeBuiltSubMesh> BuildSubMeshes(const XmlNode& geometry)
{
    std::vector<DaeBuiltSubMesh> result;
    const XmlNode* mesh = geometry.FindChild("mesh");
    if (mesh == nullptr) {
        return result;
    }

    DaeSourceTable sources;
    sources.Load(*mesh);

    // <triangles> and <polylist> are both flat triangle/polygon sets; handle
    // them with the same builder, which branches on the element name.
    for (const XmlNode* prim : mesh->FindChildren("triangles")) {
        DaeBuiltSubMesh sub = BuildFromPrimitive(*prim, *mesh, sources);
        if (!sub.geometry.positions.empty()) {
            result.push_back(std::move(sub));
        }
    }
    for (const XmlNode* prim : mesh->FindChildren("polylist")) {
        DaeBuiltSubMesh sub = BuildFromPrimitive(*prim, *mesh, sources);
        if (!sub.geometry.positions.empty()) {
            result.push_back(std::move(sub));
        }
    }
    return result;
}

} // namespace Concord::Asset::Dae
