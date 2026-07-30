#ifndef CONCORD_IMPORTMODELVALIDATOR_H
#define CONCORD_IMPORTMODELVALIDATOR_H

#include "engine/asset/import/ImportBudget.h"
#include "engine/asset/import/ImportedModel.h"

namespace Concord::Asset {

/** Charges completed geometry and rejects malformed or over-budget output. */
bool ValidateImportedModel(const ImportedModel& model,
                           ImportBudget& budget) noexcept;

} // namespace Concord::Asset

#endif // CONCORD_IMPORTMODELVALIDATOR_H
