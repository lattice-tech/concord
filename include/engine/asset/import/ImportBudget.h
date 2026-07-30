#ifndef CONCORD_IMPORTBUDGET_H
#define CONCORD_IMPORTBUDGET_H

#include <cstddef>

namespace Concord::Asset {

/** @brief Resource ceilings applied consistently to one model import. */
struct ImportLimits {
    std::size_t maxPrimaryFileBytes = 256u * 1024u * 1024u;
    std::size_t maxDependencyFileBytes = 256u * 1024u * 1024u;
    std::size_t maxTotalInputBytes = 512u * 1024u * 1024u;
    std::size_t maxDecodedBytes = 512u * 1024u * 1024u;
    std::size_t maxStringBytes = 64u * 1024u * 1024u;
    std::size_t maxVertices = 4'000'000u;
    std::size_t maxFaces = 4'000'000u;
    std::size_t maxIndices = 24'000'000u;
    std::size_t maxSubMeshes = 65'536u;
    std::size_t maxDocumentNodes = 1'000'000u;
    std::size_t maxDependencies = 1'024u;
    std::size_t maxNestingDepth = 256u;
    std::size_t maxHeaderBytes = 1u * 1024u * 1024u;
    std::size_t maxLineBytes = 1u * 1024u * 1024u;
    std::size_t maxListItems = 65'536u;
};

/** @brief Transactional counters for attacker-controlled import resources. */
class ImportBudget {
public:
    explicit ImportBudget(ImportLimits limits = {}) : m_limits(limits) {}

    const ImportLimits& Limits() const noexcept { return m_limits; }
    bool ConsumePrimaryBytes(std::size_t amount) noexcept;
    bool ConsumeDependencyBytes(std::size_t amount) noexcept;
    bool ConsumeDecodedBytes(std::size_t amount) noexcept;
    bool ConsumeStringBytes(std::size_t amount) noexcept;
    bool ConsumeVertices(std::size_t amount) noexcept;
    bool ConsumeFaces(std::size_t amount) noexcept;
    bool ConsumeIndices(std::size_t amount) noexcept;
    bool ConsumeSubMeshes(std::size_t amount) noexcept;
    bool ConsumeDocumentNodes(std::size_t amount) noexcept;
    bool ConsumeDependency() noexcept;
    bool AllowsDepth(std::size_t depth) const noexcept;

    std::size_t InputBytes() const noexcept { return m_inputBytes; }

private:
    static bool Consume(std::size_t amount, std::size_t limit,
                        std::size_t& used) noexcept;

    ImportLimits m_limits;
    std::size_t m_inputBytes = 0;
    std::size_t m_decodedBytes = 0;
    std::size_t m_stringBytes = 0;
    std::size_t m_vertices = 0;
    std::size_t m_faces = 0;
    std::size_t m_indices = 0;
    std::size_t m_subMeshes = 0;
    std::size_t m_documentNodes = 0;
    std::size_t m_dependencies = 0;
};

} // namespace Concord::Asset

#endif // CONCORD_IMPORTBUDGET_H
