#ifndef CONCORD_DAE_NODEWALKER_H
#define CONCORD_DAE_NODEWALKER_H

#include "engine/asset/import/dae/XmlNode.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Concord::Asset::Dae {

/**
 * One `<instance_geometry>` encountered while walking the scene's node
 * hierarchy, with its accumulated world transform and material bindings ready
 * for the importer to use.
 */
struct DaeNodeInstance {
    /** Geometry URL as written, e.g. "#Cube-geometry" (leading '#' kept). */
    std::string geometryUrl;

    /** Column-major 4x4 world transform accumulated from ancestor `<node>`s. */
    std::array<float, 16> transform{};

    /** Maps each material symbol the geometry declares to a material id. */
    std::vector<std::pair<std::string, std::string>> materialBindings;
};

/**
 * Collects every `<instance_geometry>` in the document's default visual scene,
 * each carrying the composed transform of its `<node>` chain.
 *
 * Collada transforms (`<matrix>`, `<translate>`, `<rotate>`, `<scale>`) are
 * applied in document order and post-multiplied (OpenGL convention), so a
 * node's world transform is `parentWorld * localChain`. The default scene (or
 * the only scene when one is declared) is walked; `<instance_node>` references
 * are followed so instanced sub-trees are included.
 *
 * @param root The parsed document root.
 * @return One entry per `<instance_geometry>`, in traversal order.
 */
std::vector<DaeNodeInstance> CollectInstances(const XmlNode& root);

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_NODEWALKER_H
