#ifndef CONCORD_VIEWEFFECTSTATE_H
#define CONCORD_VIEWEFFECTSTATE_H

#include <cstdint>
#include <type_traits>

namespace Concord {

/**
 * @brief Plain per-frame screen-effect data copied to the render thread.
 *
 * This type owns no resources and contains no authoring-time state. The render
 * backend may pack these fields into uniforms without retaining pointers into
 * the active Camera or ScreenEffectStack.
 */
struct ViewEffectState {
    /** Current deterministic screen-shake displacement in output pixels. */
    float shakeOffsetPixels[2]{0.0f, 0.0f};

    /** Magnifier center in normalized viewport coordinates; (0, 0) is bottom-left. */
    float magnifierCenter[2]{0.5f, 0.5f};

    /** Magnifier radius relative to the shorter viewport dimension. */
    float magnifierRadius = 0.25f;

    /** Magnifier zoom factor, always at least one. */
    float magnifierZoom = 2.0f;

    /** Signed radial magnifier distortion. */
    float magnifierDistortion = 0.0f;

    /** Soft magnifier edge width, clamped to its radius. */
    float magnifierFeather = 0.03f;

    /** One when the magnifier is enabled and has a non-zero radius. */
    std::uint32_t magnifierEnabled = 0;

    /** Lens-flare sun position in normalized viewport coords; filled per frame by the backend. */
    float lensFlareSunPos[2]{0.5f, 0.5f};

    /** Lens-flare strength; zero disables. Set by the effect stack. */
    float lensFlareIntensity = 0.0f;

    /** One when lens flare is enabled by the effect stack. */
    std::uint32_t lensFlareEnabled = 0;

    /** One when the backend resolved an on-screen, in-front sun this frame. */
    std::uint32_t lensFlareSunVisible = 0;
};

static_assert(std::is_standard_layout_v<ViewEffectState>);
static_assert(std::is_trivially_copyable_v<ViewEffectState>);

} // namespace Concord

#endif // CONCORD_VIEWEFFECTSTATE_H
