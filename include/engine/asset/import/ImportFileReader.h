#ifndef CONCORD_IMPORTFILEREADER_H
#define CONCORD_IMPORTFILEREADER_H

#include "engine/asset/import/ImportContext.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Concord::Asset {

/** Charges and validates the canonical primary file before parser allocation. */
bool ValidatePrimaryFile(ImportContext& context) noexcept;

/** Resolves, charges, and reads one dependency exactly. */
bool ReadDependencyFile(ImportContext& context, std::string_view reference,
                        std::vector<std::uint8_t>& bytes,
                        std::filesystem::path* resolvedPath = nullptr);

} // namespace Concord::Asset

#endif // CONCORD_IMPORTFILEREADER_H
