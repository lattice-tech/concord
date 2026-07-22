#ifndef CONCORD_DAE_PRIMITIVEINPUTS_H
#define CONCORD_DAE_PRIMITIVEINPUTS_H

#include "engine/asset/import/dae/XmlNode.h"

#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset::Dae {

/** One `<input>` declaration inside a `<triangles>`/`<polylist>` element. */
struct InputInfo {
    std::string semantic;
    std::string source;
    int offset = 0;
};

/**
 * One triangle-corner's attribute index triple, read from `<p>`. Each field is
 * an index into the corresponding source (position, normal, uv), or -1 when the
 * primitive declared no input for that semantic.
 */
struct CornerSet {
    int pos;
    int nrm;
    int uv;
};

/** Splits a space-delimited index list (the body of `<p>` or `<vcount>`) into ints. */
std::vector<int> ParseIndexList(const std::string& text);

/**
 * Collects the `<input>` children of `parent`, sorted by offset so the index
 * sets in `<p>` line up with the semantic order. Returns the set width (max
 * offset + 1), which is how many indices each vertex corner contributes.
 */
int CollectInputs(const XmlNode& parent, std::vector<InputInfo>& out);

/**
 * Resolves the VERTEX semantic through the `<vertices>` element to the
 * underlying POSITION source. Collada wraps the position source in `<vertices>`
 * so the VERTEX input references the wrapper, not the data directly.
 */
std::string ResolveVertexPositionSource(const XmlNode& mesh, std::string_view verticesId);

/** Finds the source ref for `semantic` among the inputs, resolving VERTEX. */
std::string FindSourceForSemantic(const std::vector<InputInfo>& inputs,
                                  const XmlNode& mesh,
                                  std::string_view semantic);

/** Finds the offset for `semantic` among the inputs, or -1 when absent. */
int FindOffsetForSemantic(const std::vector<InputInfo>& inputs, std::string_view semantic);

/**
 * Reads the flat `<p>` index list of `primitive` into per-corner attribute
 * triples. For `<triangles>` every `setWidth` indices form one corner; for
 * `<polylist>` the `<vcount>` sizes are respected so corners stay grouped by
 * polygon for later fan triangulation. Returns an empty vector when the
 * primitive is malformed (missing `<p>`, or a `<polylist>` missing `<vcount>`).
 */
std::vector<CornerSet> CollectCorners(const XmlNode& primitive,
                                      int setWidth,
                                      int posOffset,
                                      int nrmOffset,
                                      int uvOffset);

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_PRIMITIVEINPUTS_H
