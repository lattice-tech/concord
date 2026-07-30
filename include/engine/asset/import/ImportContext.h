#ifndef CONCORD_IMPORTCONTEXT_H
#define CONCORD_IMPORTCONTEXT_H

#include "engine/asset/import/ImportBudget.h"
#include "engine/asset/import/ImportPathSandbox.h"

#include <filesystem>
#include <optional>

namespace Concord::Asset {

struct ImportOptions {
    ImportLimits limits{};
    std::optional<std::filesystem::path> allowedRoot;
};

/** @brief Security and resource state shared by every helper in one import. */
class ImportContext {
public:
    ImportContext(ImportBudget budget, ImportPathSandbox paths)
        : m_budget(std::move(budget)), m_paths(std::move(paths)) {}

    ImportBudget& Budget() noexcept { return m_budget; }
    const ImportBudget& Budget() const noexcept { return m_budget; }
    const ImportPathSandbox& Paths() const noexcept { return m_paths; }

private:
    ImportBudget m_budget;
    ImportPathSandbox m_paths;
};

} // namespace Concord::Asset

#endif // CONCORD_IMPORTCONTEXT_H
