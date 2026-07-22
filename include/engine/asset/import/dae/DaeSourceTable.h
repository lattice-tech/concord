#ifndef CONCORD_DAE_SOURCETABLE_H
#define CONCORD_DAE_SOURCETABLE_H

#include "engine/asset/import/dae/XmlNode.h"

#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset::Dae {

/**
 * One Collada `<source>`: a typed data array plus the accessor that gives it
 * structure (stride and parameter names). Positions, normals and UVs each live
 * in their own `<source>`, referenced by `<input>` elements inside
 * `<triangles>` or `<polylist>`.
 */
struct DaeSource {
    /** Id matching the `<source id="...">` attribute (without the leading '#'). */
    std::string id;

    /** Parsed `<float_array>` contents; empty when the source is int/name-typed. */
    std::vector<float> floats;

    /** Parsed `<Name_array>` or `<int_array>` contents; empty for float sources. */
    std::vector<int> ints;

    /** Element count per logical entry, from `<accessor stride="...">` (default 1). */
    int stride = 1;
};

/**
 * Holds every `<source>` declared inside one `<mesh>`, keyed by id.
 *
 * Collada references sources by URL fragment (e.g. "#mesh-positions"); the
 * lookup strips the leading '#'. Built once per geometry and queried by the
 * mesh builder as it resolves each `<input>` semantic.
 */
class DaeSourceTable {
public:
    /** Parses every `<source>` child of `mesh` into the table. */
    void Load(const XmlNode& mesh);

    /** The source whose id matches `ref` (with or without leading '#'), or nullptr. */
    const DaeSource* Find(std::string_view ref) const noexcept;

private:
    std::vector<DaeSource> m_sources;
};

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_SOURCETABLE_H
