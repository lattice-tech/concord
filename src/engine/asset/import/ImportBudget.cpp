#include "engine/asset/import/ImportBudget.h"

namespace Concord::Asset {

bool ImportBudget::Consume(std::size_t amount, std::size_t limit,
                           std::size_t& used) noexcept
{
    if (used > limit || amount > limit - used) {
        return false;
    }
    used += amount;
    return true;
}

bool ImportBudget::ConsumePrimaryBytes(std::size_t amount) noexcept
{
    return amount <= m_limits.maxPrimaryFileBytes
        && Consume(amount, m_limits.maxTotalInputBytes, m_inputBytes);
}

bool ImportBudget::ConsumeDependencyBytes(std::size_t amount) noexcept
{
    return amount <= m_limits.maxDependencyFileBytes
        && Consume(amount, m_limits.maxTotalInputBytes, m_inputBytes);
}

bool ImportBudget::ConsumeDecodedBytes(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxDecodedBytes, m_decodedBytes);
}

bool ImportBudget::ConsumeStringBytes(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxStringBytes, m_stringBytes);
}

bool ImportBudget::ConsumeVertices(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxVertices, m_vertices);
}

bool ImportBudget::ConsumeFaces(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxFaces, m_faces);
}

bool ImportBudget::ConsumeIndices(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxIndices, m_indices);
}

bool ImportBudget::ConsumeSubMeshes(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxSubMeshes, m_subMeshes);
}

bool ImportBudget::ConsumeDocumentNodes(std::size_t amount) noexcept
{
    return Consume(amount, m_limits.maxDocumentNodes, m_documentNodes);
}

bool ImportBudget::ConsumeDependency() noexcept
{
    return Consume(1, m_limits.maxDependencies, m_dependencies);
}

bool ImportBudget::AllowsDepth(std::size_t depth) const noexcept
{
    return depth <= m_limits.maxNestingDepth;
}

} // namespace Concord::Asset
