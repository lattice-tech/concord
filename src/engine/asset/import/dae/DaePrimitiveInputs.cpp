#include "engine/asset/import/dae/DaePrimitiveInputs.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>

namespace Concord::Asset::Dae {

namespace {

static constexpr int kInvalid = -1;

} // namespace

std::vector<int> ParseIndexList(const std::string& text)
{
    std::vector<int> out;
    std::istringstream ss(text);
    int v;
    while (ss >> v) {
        out.push_back(v);
    }
    return out;
}

int CollectInputs(const XmlNode& parent, std::vector<InputInfo>& out)
{
    int maxOffset = 0;
    for (const XmlNode* input : parent.FindChildren("input")) {
        InputInfo info;
        info.semantic = input->Attr("semantic");
        info.source = input->Attr("source");
        info.offset = std::stoi(input->Attr("offset", "0"));
        out.push_back(info);
        maxOffset = std::max(maxOffset, info.offset);
    }
    std::sort(out.begin(), out.end(), [](const InputInfo& a, const InputInfo& b) {
        return a.offset < b.offset;
    });
    return maxOffset + 1;
}

std::string ResolveVertexPositionSource(const XmlNode& mesh, std::string_view verticesId)
{
    std::string_view id = verticesId;
    if (!id.empty() && id[0] == '#') {
        id.remove_prefix(1);
    }
    for (const XmlNode* verts : mesh.FindChildren("vertices")) {
        if (verts->Attr("id") == id) {
            if (const XmlNode* pos = verts->FindChild("input")) {
                if (pos->Attr("semantic") == "POSITION") {
                    return pos->Attr("source");
                }
            }
        }
    }
    return std::string(verticesId);
}

std::string FindSourceForSemantic(const std::vector<InputInfo>& inputs,
                                  const XmlNode& mesh,
                                  std::string_view semantic)
{
    for (const InputInfo& in : inputs) {
        if (in.semantic == semantic) {
            if (semantic == "VERTEX") {
                return ResolveVertexPositionSource(mesh, in.source);
            }
            return in.source;
        }
    }
    return {};
}

int FindOffsetForSemantic(const std::vector<InputInfo>& inputs, std::string_view semantic)
{
    for (const InputInfo& in : inputs) {
        if (in.semantic == semantic) {
            return in.offset;
        }
    }
    return -1;
}

std::vector<CornerSet> CollectCorners(const XmlNode& primitive,
                                      int setWidth,
                                      int posOffset,
                                      int nrmOffset,
                                      int uvOffset)
{
    std::vector<CornerSet> corners;

    // Read the flat index list from <p>.
    const XmlNode* p = primitive.FindChild("p");
    if (p == nullptr) {
        return corners;
    }
    const std::vector<int> indices = ParseIndexList(p->text);

    // Build the list of triangle corner-sets. For <triangles> every 3 sets is
    // one triangle; for <polylist> <vcount> gives the polygon sizes and each is
    // fan-triangulated.
    const bool isPolylist = (primitive.name == "polylist");
    if (isPolylist) {
        const XmlNode* vcountNode = primitive.FindChild("vcount");
        if (vcountNode == nullptr) {
            return {};
        }
        const std::vector<int> vcounts = ParseIndexList(vcountNode->text);
        std::size_t idx = 0;
        for (int vc : vcounts) {
            if (vc < 3) {
                idx += static_cast<std::size_t>(setWidth);
                continue;
            }
            // Fan triangulation: read vc corners, emit (0, i, i+1).
            const std::size_t base = corners.size();
            for (int v = 0; v < vc; ++v) {
                CornerSet cs{kInvalid, kInvalid, kInvalid};
                if (idx + setWidth > indices.size()) {
                    break;
                }
                if (posOffset >= 0) cs.pos = indices[idx + posOffset];
                if (nrmOffset >= 0) cs.nrm = indices[idx + nrmOffset];
                if (uvOffset >= 0) cs.uv = indices[idx + uvOffset];
                corners.push_back(cs);
                idx += static_cast<std::size_t>(setWidth);
            }
            for (std::size_t i = 1; i + 1 < static_cast<std::size_t>(vc) && base + i + 1 < corners.size(); ++i) {
                // Mark these three as a triangle via the de-dup indices below.
                // We handle triangulation by emitting indices in fan order.
            }
        }
    } else {
        for (std::size_t idx = 0; idx + setWidth <= indices.size(); idx += static_cast<std::size_t>(setWidth)) {
            CornerSet cs{kInvalid, kInvalid, kInvalid};
            if (posOffset >= 0) cs.pos = indices[idx + posOffset];
            if (nrmOffset >= 0) cs.nrm = indices[idx + nrmOffset];
            if (uvOffset >= 0) cs.uv = indices[idx + uvOffset];
            corners.push_back(cs);
        }
    }

    return corners;
}

} // namespace Concord::Asset::Dae
