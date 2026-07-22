#ifndef CONCORD_BGFXMATHCONVERTERS_H
#define CONCORD_BGFXMATHCONVERTERS_H

#include "engine/material/BlendMode.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/DepthTest.h"
#include "engine/render/backend/RenderBackendType.h"
#include "engine/window/MsaaLevel.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord::RenderDetail {

/** Maps a Concord depth-comparison onto its bgfx state flag. */
inline std::uint64_t ToBgfxDepthTest(DepthTest test)
{
    switch (test) {
        case DepthTest::Never:        return BGFX_STATE_DEPTH_TEST_NEVER;
        case DepthTest::Less:         return BGFX_STATE_DEPTH_TEST_LESS;
        case DepthTest::LessEqual:    return BGFX_STATE_DEPTH_TEST_LEQUAL;
        case DepthTest::Equal:        return BGFX_STATE_DEPTH_TEST_EQUAL;
        case DepthTest::GreaterEqual: return BGFX_STATE_DEPTH_TEST_GEQUAL;
        case DepthTest::Greater:      return BGFX_STATE_DEPTH_TEST_GREATER;
        case DepthTest::Always:       return BGFX_STATE_DEPTH_TEST_ALWAYS;
    }
    return BGFX_STATE_DEPTH_TEST_LEQUAL;
}

/**
 * Maps a Concord cull mode onto its bgfx state flag.
 * Concord meshes are CCW-outward (AGENTS.md P1), so the back/interior faces wind
 * CW and are removed by `CULL_CCW`; `Front` removes the outward CCW faces instead.
 */
inline std::uint64_t ToBgfxCull(CullMode mode)
{
    switch (mode) {
        case CullMode::Back:  return BGFX_STATE_CULL_CCW;
        case CullMode::Front: return BGFX_STATE_CULL_CW;
        case CullMode::None:  return 0;
    }
    return BGFX_STATE_CULL_CCW;
}

/**
 * Maps a Concord blend mode onto its bgfx blend-state flag.
 * Opaque returns 0 (no blend bits, source overwrites destination). Additive
 * uses (SRC_ALPHA, ONE), allowing a particle's lifetime alpha to fade emitted
 * energy while remaining order-independent. Alpha uses
 * (SRC_ALPHA, INV_SRC_ALPHA) for ordinary translucency. Both transparent modes
 * are issued after opaque geometry with depth writes disabled.
 */
inline std::uint64_t ToBgfxBlend(Material::BlendMode mode)
{
    switch (mode) {
        case Material::BlendMode::Opaque:   return 0;
        case Material::BlendMode::Alpha:    return BGFX_STATE_BLEND_ALPHA;
        case Material::BlendMode::Additive:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    }
    return 0;
}

/** Maps a Concord MSAA level onto the `bgfx::reset` flag bits. */
inline std::uint32_t ToBgfxResetFlags(MsaaLevel level)
{
    switch (level) {
        case MsaaLevel::Off:  return BGFX_RESET_NONE;
        case MsaaLevel::X2:   return BGFX_RESET_MSAA_X2;
        case MsaaLevel::X4:   return BGFX_RESET_MSAA_X4;
        case MsaaLevel::X8:   return BGFX_RESET_MSAA_X8;
        case MsaaLevel::X16:  return BGFX_RESET_MSAA_X16;
    }
    return BGFX_RESET_MSAA_X4;
}

/**
 * Maps the engine's API selection onto bgfx's renderer enum.
 *
 * Always Vulkan — including `Auto`. Never returns Direct3D* or Count (which
 * would let bgfx pick a D3D default on Windows).
 */
inline bgfx::RendererType::Enum ToBgfxRenderer(RenderBackendType /*type*/)
{
    return bgfx::RendererType::Vulkan;
}

/** Unpacks a 0xRRGGBBAA packed color into a [0,1] float4 suitable for vec4 uniforms. */
inline void ColorToFloat4(float rgba[4], std::uint32_t color) noexcept
{
    rgba[0] = static_cast<float>((color >> 24) & 0xff) / 255.0f;
    rgba[1] = static_cast<float>((color >> 16) & 0xff) / 255.0f;
    rgba[2] = static_cast<float>((color >>  8) & 0xff) / 255.0f;
    rgba[3] = static_cast<float>((color >>  0) & 0xff) / 255.0f;
}

/**
 * Writes `src` into `dst` transposed (generic math utility only).
 *
 * **Do not use for instance world matrices.** The mesh VS multiplies with an
 * explicit column blend (`col0*x + col1*y + col2*z + col3`) after a plain
 * `memcpy` of the bx column-major matrix. CPU-side transpose + mat4 rebuild
 * is what broke multi-backend placement historically — keep one Vulkan-only
 * contract.
 */
inline void TransposeMatrix(float dst[16], const float src[16]) noexcept
{
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            dst[col * 4 + row] = src[row * 4 + col];
        }
    }
}

} // namespace Concord::RenderDetail

#endif // CONCORD_BGFXMATHCONVERTERS_H
