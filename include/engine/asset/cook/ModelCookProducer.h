#ifndef CONCORD_MODELCOOKPRODUCER_H
#define CONCORD_MODELCOOKPRODUCER_H

#include "engine/asset/cook/CookSession.h"

namespace Concord::Asset {

/**
 * @brief The cook producer used by the offline cooker for real asset baking.
 *
 * For AssetType::Mesh entries it imports the source model file (OBJ, glTF,
 * DAE, ...) through the shared importer registry and encodes the result as a
 * CookedModel container, so the runtime can load fully parsed geometry
 * without touching the development source format. Skinned models are not yet
 * representable in CookedModel (M1) and fall back to the passthrough
 * encoding, as does every non-mesh asset type; the runtime detects the
 * container magic and falls back to live import when a cooked blob is not a
 * CookedModel.
 *
 * The returned callable matches CookProduceFn and is safe to hand straight to
 * CookSession.
 */
CookProduceFn MakeModelCookProduce();

} // namespace Concord::Asset

#endif // CONCORD_MODELCOOKPRODUCER_H
