#ifndef CONCORD_BOXDESC_H
#define CONCORD_BOXDESC_H

#include "engine/material/MaterialDesc.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/object/Transform.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Object {

/**
 * Every field a Box can be constructed or Set() from.
 *
 * A plain aggregate so a caller can build one with designated
 * initializers and only name the fields it actually wants to change, e.g.
 * `BoxDesc{.color = 0xff0000ff}`. Set() replaces a Box's description
 * wholesale (the same semantics as Window::Set) — a field left unnamed
 * falls back to *this type's* default, not to whatever the Box's current
 * value happens to be, so a partial update must copy any fields it wants
 * to keep from Box::Desc() first.
 *
 * `size` is the box's full extent along each axis in world units (not
 * half-extents). The engine renders it by scaling a shared unit cube
 * (±1 on each axis) to match.
 */
struct BoxDesc {
    Transform transform{};

    /**
     * Which built-in mesh to draw. Defaults to a cube, so existing callers
     * are unaffected; set it to render a sphere, cylinder or cone instead
     * (the `size` field then scales that shape the same way).
     */
    PrimitiveShape shape = PrimitiveShape::Cube;

    /** Full width/height/depth in world units. */
    Vector3 size{1.0f, 1.0f, 1.0f};

    /**
     * Shorthand for a flat base color, packed 0xRRGGBBAA. Convenience for the
     * common `.color = COLOR_RED` case: it seeds `material.surface.albedo`
     * unless the material block already sets an explicit (non-white) albedo,
     * in which case the material wins. For anything beyond a base color
     * (metallic, roughness, gradient, unlit) use `material` directly.
     */
    std::uint32_t color = 0xffffffff;

    /**
     * Full surface description (lighting model, metallic/roughness, gradient,
     * textures). Defaults to a plain lit white surface; see Material::MaterialDesc.
     */
    Material::MaterialDesc material{};
};

} // namespace Concord::Object

#endif // CONCORD_BOXDESC_H
