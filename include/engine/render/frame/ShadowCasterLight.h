#ifndef CONCORD_SHADOWCASTERLIGHT_H
#define CONCORD_SHADOWCASTERLIGHT_H

#include "engine/render/frame/RenderLight.h"

#include <cstdint>

namespace Concord {

/**
 * @brief Index of the light whose shadows the frame renders, or -1 for none.
 *
 * The first shadow-casting directional light wins, matching the "single
 * shadow-casting sun" model the lighting path implements. Extraction and the
 * render backend must agree on this choice, so both go through this helper
 * rather than repeating the rule.
 */
inline int FindShadowCastingLight(const RenderLight* lights, std::uint32_t count) noexcept
{
    if (lights == nullptr) {
        return -1;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (lights[i].type == LightType::Directional && lights[i].castShadow) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace Concord

#endif // CONCORD_SHADOWCASTERLIGHT_H
