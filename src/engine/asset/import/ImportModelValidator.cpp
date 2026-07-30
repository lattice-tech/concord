#include "engine/asset/import/ImportModelValidator.h"

namespace Concord::Asset {

bool ValidateImportedModel(const ImportedModel& model,
                           ImportBudget& budget) noexcept
{
    if (!budget.ConsumeSubMeshes(model.meshes.size())) {
        return false;
    }
    for (const ImportedSubMesh& subMesh : model.meshes) {
        const MeshData& geometry = subMesh.geometry;
        const std::size_t vertexCount = geometry.positions.size();
        const std::size_t indexCount = geometry.indices32.empty()
            ? geometry.indices.size() : geometry.indices32.size();
        if (indexCount % 3u != 0u
            || (!geometry.normals.empty() && geometry.normals.size() != vertexCount)
            || (!geometry.uvs.empty() && geometry.uvs.size() != vertexCount)
            || (!geometry.boneIndices.empty()
                && geometry.boneIndices.size() != vertexCount)
            || (!geometry.boneWeights.empty()
                && geometry.boneWeights.size() != vertexCount)
            || !budget.ConsumeVertices(vertexCount)
            || !budget.ConsumeIndices(indexCount)
            || !budget.ConsumeFaces(indexCount / 3u)) {
            return false;
        }
        for (std::uint16_t index : geometry.indices) {
            if (index >= vertexCount) {
                return false;
            }
        }
        for (std::uint32_t index : geometry.indices32) {
            if (index >= vertexCount) {
                return false;
            }
        }
    }
    return true;
}

} // namespace Concord::Asset
