#ifndef CONCORD_DRAWOPTIONS_H
#define CONCORD_DRAWOPTIONS_H

#include "engine/material/BlendMode.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/DepthTest.h"

namespace Concord::Material {

/**
 * How a surface participates in depth, occlusion, and draw ordering — the
 * knobs behind custom visibility effects, kept orthogonal to what the surface
 * looks like (Surface) so the two compose freely.
 *
 * The defaults are ordinary opaque behavior: test depth, write depth, cull
 * back faces, neutral order. Change them for effects the engine does not need
 * to bake in as special cases:
 *   - An always-visible marker: `{.depthTest = DepthTest::Always, .priority = 100}`.
 *   - A double-sided card or banner: `{.cull = CullMode::None}`.
 *   - A see-through/hologram overlay that never occludes: `{.depthWrite = false}`.
 *   - Reveal-behind-walls highlight: `{.depthTest = DepthTest::Greater, .depthWrite = false}`.
 *
 * A plain aggregate so a caller names only what it changes, e.g.
 * `.draw = {.cull = CullMode::None}`.
 */
struct DrawOptions {
    /**
     * How the fragment composites with the frame buffer (default Opaque).
     * Alpha/Additive mark a surface as transparent: the backend draws it after
     * all opaque geometry with depth writes disabled. Additive is the glowing,
     * order-independent mode used by particles, fire and magic.
     */
    BlendMode blend = BlendMode::Opaque;

    /** Depth comparison deciding which pixels survive (default LessEqual). */
    DepthTest depthTest = DepthTest::LessEqual;

    /** Whether the surface writes its depth, so later draws test against it. */
    bool depthWrite = true;

    /** Which faces to discard (default Back for closed solids). */
    CullMode cull = CullMode::Back;

    /**
     * Draw-order bias: draws run from lowest priority to highest, so a higher
     * value renders later (on top of, and over the depth of, lower ones).
     * Objects sharing a priority resolve purely by depth. Leave at 0 for
     * ordinary depth-sorted opaque geometry.
     */
    int priority = 0;
};

} // namespace Concord::Material

#endif // CONCORD_DRAWOPTIONS_H
