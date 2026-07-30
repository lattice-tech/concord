#ifndef CONCORD_SHADOWCASTERCULLER_H
#define CONCORD_SHADOWCASTERCULLER_H

#include "engine/collision/Aabb.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/spatial/Frustum.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Concord::Spatial {

/**
 * @brief True when @p bounds can darken something inside @p frustum.
 *
 * The box is swept along the light's travel direction for @p extrusion world
 * units and the swept volume is tested against the view frustum: that is the
 * region the caster's shadow can occupy. @p extrusion must match the shadow
 * frustum's caster extrusion, since geometry farther up-light than that is
 * outside the shadow map's depth range anyway.
 */
bool ShadowCasterTouchesFrustum(const Frustum& frustum, const Collision::Aabb& bounds,
                                const float lightDirection[3], float extrusion) noexcept;

/**
 * @brief Picks the frustum-rejected draws that still cast into the frustum.
 *
 * @param authored Every draw extraction collected this frame.
 * @param culledIndices Indices CullInstances rejected, as it reported them.
 * @param lightDirection Direction the shadow-casting light travels; a
 *        non-finite or zero direction selects nothing.
 * @param out Cleared, then filled in ascending authored order.
 * @return The number of casters written to @p out.
 */
std::uint32_t SelectShadowCasters(const std::vector<RenderInstance>& authored,
                                  const std::vector<std::size_t>& culledIndices,
                                  const Frustum& frustum, const float lightDirection[3],
                                  float extrusion, std::vector<RenderInstance>& out);

} // namespace Concord::Spatial

#endif // CONCORD_SHADOWCASTERCULLER_H
