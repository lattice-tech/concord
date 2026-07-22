#ifndef CONCORD_GRADIENTAXIS_H
#define CONCORD_GRADIENTAXIS_H

namespace Concord::Material {

/**
 * The local-space axis a gradient runs along.
 *
 * A gradient interpolates between two colors across the primitive's own
 * unit range (±1 on each axis, before the object's size/transform), so the
 * blend is stable no matter how the object is scaled, moved or rotated in
 * the world. The chosen axis coordinate is remapped from [-1, 1] to [0, 1]
 * and used as the mix factor (see Gradient).
 */
enum class GradientAxis {
    /** Left-to-right, along local X. */
    X,

    /** Bottom-to-top, along local Y. */
    Y,

    /** Back-to-front, along local Z. */
    Z,
};

/** Canonical, human-readable name of a GradientAxis (never null). */
inline const char* ToString(GradientAxis axis) noexcept
{
    switch (axis) {
        case GradientAxis::X: return "X";
        case GradientAxis::Y: return "Y";
        case GradientAxis::Z: return "Z";
    }
    return "Y";
}

} // namespace Concord::Material

#endif // CONCORD_GRADIENTAXIS_H
