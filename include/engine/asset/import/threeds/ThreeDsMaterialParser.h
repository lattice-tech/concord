#ifndef CONCORD_THREEDS_MATERIALPARSER_H
#define CONCORD_THREEDS_MATERIALPARSER_H

#include "engine/asset/import/threeds/ThreeDsChunkReader.h"
#include "engine/material/MaterialDesc.h"

#include <string>
#include <vector>

namespace Concord::Asset::ThreeDs {

/**
 * One material parsed from a 3DS material-entry (0xAFFF) chunk.
 *
 * 3DS predates PBR, so the mapping is approximate: the diffuse color becomes
 * albedo, shininess becomes roughness (inverted), and a diffuse texture map
 * becomes the albedo texture. Metallic stays 0 (dielectric).
 */
struct ParsedMaterial {
    /** Material name, matched against ParsedMesh::faceMaterials. */
    std::string name;

    /** The resolved Concord material descriptor. */
    Material::MaterialDesc desc;
};

/**
 * Parses every material-entry (0xAFFF) chunk under the EDIT3DS (0x3D3D) chunk.
 *
 * Reads the material name (0xA000), ambient/diffuse/specular colors
 * (0xA010/0xA020/0xA030), shininess (0xA040), transparency (0xA050) and the
 * diffuse texture map (0xA200 with filename 0xA300). Color sub-chunks accept
 * both byte (0x0010) and float (0x0012) encodings; percentage sub-chunks
 * accept both int (0x0030) and float (0x0031).
 *
 * @param reader Positioned at the start of the EDIT3DS chunk's data.
 * @param edit The EDIT3DS chunk (bounds the scan).
 * @return One ParsedMaterial per material entry, in file order.
 */
std::vector<ParsedMaterial> ParseMaterials(ChunkReader& reader, const Chunk& edit);

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDS_MATERIALPARSER_H
