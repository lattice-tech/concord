#ifndef CONCORD_ANTIALIASING_H
#define CONCORD_ANTIALIASING_H

#include "engine/window/MsaaLevel.h"

namespace Concord {

/**
 * The full-scene anti-aliasing technique the engine applies.
 *
 * Selected once from the config file (`antialiasing=` key) and applied to
 * every window. Two families are represented:
 *  - Hardware multisampling (MSAA). These modes are retained for the pending
 *    multisampled HDR-target implementation; the current single-sample HDR
 *    path cannot provide real MSAA coverage yet.
 *  - Post-process AA (FXAA, SMAA), a screen-space pass over the rendered
 *    image. Smooths every edge including shader/specular aliasing, at the
 *    cost of a little blur and an extra fullscreen pass.
 *
 * Hardware MSAA remains disabled until the HDR target has matching
 * multisampled color/depth attachments and an explicit resolve. Selecting an
 * MSAA enum therefore uses the safe single-sample path rather than resetting
 * an incompatible swap chain.
 */
enum class AntiAliasing {
    /** No anti-aliasing. */
    Off,

    /** 2x hardware multisampling. */
    Msaa2,

    /** 4x hardware multisampling. */
    Msaa4,

    /** 8x hardware multisampling. */
    Msaa8,

    /** Fast approximate AA: one screen-space pass, cheapest post-process AA. */
    Fxaa,

    /** SMAA 1x High preset. The legacy `2` suffix is a quality label, not 2x sampling. */
    Smaa2,

    /** SMAA 1x Ultra preset. The legacy `4` suffix is a quality label, not 4x sampling. */
    Smaa4,
};

/** True when `aa` is a hardware-multisampling mode (as opposed to post-process). */
inline bool IsMsaa(AntiAliasing aa) noexcept
{
    return aa == AntiAliasing::Msaa2 || aa == AntiAliasing::Msaa4 || aa == AntiAliasing::Msaa8;
}

/** True when `aa` is a screen-space post-process technique (FXAA/SMAA). */
inline bool IsPostProcess(AntiAliasing aa) noexcept
{
    return aa == AntiAliasing::Fxaa || aa == AntiAliasing::Smaa2 || aa == AntiAliasing::Smaa4;
}

/**
 * The swap-chain MSAA level this mode can safely use with the current HDR path.
 * All modes remain single-sample until an explicit HDR MSAA resolve exists.
 */
inline MsaaLevel ToMsaaLevel(AntiAliasing aa) noexcept
{
    (void)aa;
    return MsaaLevel::Off;
}

/** Canonical, human-readable name of an AntiAliasing mode (never null). */
inline const char* ToString(AntiAliasing aa) noexcept
{
    switch (aa) {
        case AntiAliasing::Off:   return "Off";
        case AntiAliasing::Msaa2: return "MSAA2x";
        case AntiAliasing::Msaa4: return "MSAA4x";
        case AntiAliasing::Msaa8: return "MSAA8x";
        case AntiAliasing::Fxaa:  return "FXAA";
        case AntiAliasing::Smaa2: return "SMAA 1x High";
        case AntiAliasing::Smaa4: return "SMAA 1x Ultra";
    }
    return "Off";
}

} // namespace Concord

#endif // CONCORD_ANTIALIASING_H
