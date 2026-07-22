#ifndef CONCORD_DEPTHTEST_H
#define CONCORD_DEPTHTEST_H

namespace Concord {

/**
 * How a draw's depth is compared against what the depth buffer already holds,
 * deciding whether each pixel is kept or rejected as hidden.
 *
 * The buffer stores the nearest depth written so far, so `LessEqual` (the
 * default) keeps a pixel when it is at or nearer than what is already there —
 * ordinary solid occlusion. The others exist for effects: `Always` ignores
 * depth entirely (draw on top of everything, e.g. gizmos or an x-ray outline),
 * `Greater`/`GreaterEqual` keep only pixels *behind* existing geometry (reveal
 * what is occluded), and `Never` writes nothing. Pair with DrawOptions::priority
 * and depthWrite for fuller control over how an object participates in occlusion.
 */
enum class DepthTest {
    /** Never passes; the draw contributes no color. */
    Never,

    /** Passes when strictly nearer than the stored depth. */
    Less,

    /** Passes when nearer than or equal to the stored depth (default solid occlusion). */
    LessEqual,

    /** Passes only when exactly equal to the stored depth. */
    Equal,

    /** Passes when farther than or equal to the stored depth. */
    GreaterEqual,

    /** Passes when strictly farther than the stored depth. */
    Greater,

    /** Always passes, ignoring stored depth (draw on top). */
    Always,
};

/** Canonical, human-readable name of a DepthTest (never null). */
inline const char* ToString(DepthTest test) noexcept
{
    switch (test) {
        case DepthTest::Never:        return "Never";
        case DepthTest::Less:         return "Less";
        case DepthTest::LessEqual:    return "LessEqual";
        case DepthTest::Equal:        return "Equal";
        case DepthTest::GreaterEqual: return "GreaterEqual";
        case DepthTest::Greater:      return "Greater";
        case DepthTest::Always:       return "Always";
    }
    return "LessEqual";
}

} // namespace Concord

#endif // CONCORD_DEPTHTEST_H
