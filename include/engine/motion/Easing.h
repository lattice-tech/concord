#ifndef CONCORD_EASING_H
#define CONCORD_EASING_H

#include "Concord/CExport.h"

namespace Concord {
namespace Motion {

/**
 * Interpolation curves for tweens. Every curve maps a normalized time
 * @c t in [0,1] to an eased value in roughly [0,1] (overshooting curves like
 * OutBack briefly leave that range by design).
 */
enum class Easing {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    OutBack,
    OutBounce,
};

/**
 * Evaluates @p curve at normalized time @p t. @p t is clamped to [0,1] first,
 * so callers can pass raw elapsed/duration ratios without guarding the ends.
 */
CENGINE_API float Ease(Easing curve, float t) noexcept;

} // namespace Motion
} // namespace Concord

#endif // CONCORD_EASING_H
