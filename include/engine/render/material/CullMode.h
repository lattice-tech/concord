#ifndef CONCORD_CULLMODE_H
#define CONCORD_CULLMODE_H

namespace Concord {

/**
 * Which triangle faces a draw discards.
 *
 * Every built-in primitive winds its triangles counter-clockwise as seen from
 * outside the solid (see Primitives), so `Back` — the default — hides the
 * interior faces that can never be seen and halves the work for closed shapes.
 * `None` draws both sides, which is what thin/open geometry (planes, cards,
 * cloth) or deliberate see-through effects want. `Front` hides the near side,
 * useful for tricks like rendering the inside of a skybox or a room.
 */
enum class CullMode {
    /** Discard back faces; the standard choice for a closed, solid object. */
    Back,

    /** Discard front faces; shows the inside of a shape. */
    Front,

    /** Draw both sides; nothing is culled. */
    None,
};

/** Canonical, human-readable name of a CullMode (never null). */
inline const char* ToString(CullMode mode) noexcept
{
    switch (mode) {
        case CullMode::Back: return "Back";
        case CullMode::Front: return "Front";
        case CullMode::None: return "None";
    }
    return "Back";
}

} // namespace Concord

#endif // CONCORD_CULLMODE_H
