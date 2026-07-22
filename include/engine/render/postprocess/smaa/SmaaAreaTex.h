#ifndef CONCORD_SMAAAREATEX_H
#define CONCORD_SMAAAREATEX_H

#include <cstdint>
#include <vector>

namespace Concord {
namespace Smaa {

/**
 * Dimensions of the reference SMAA area lookup texture, in texels.
 *
 * The layout matches the precomputed `AreaTex` that ships with the reference
 * SMAA distribution: 160 wide (80 orthogonal + 80 diagonal columns) by 560 tall
 * (7 subsample blocks of 80 rows). Stored two channels per texel (R,G), which
 * the blend-weight shader reads through SMAA_AREATEX_SELECT.
 */
constexpr int kAreaTexWidth = 160;
constexpr int kAreaTexHeight = 560;

/**
 * Builds the reference SMAA area lookup texture as tightly-packed RG8 data,
 * row-major from the top-left texel (matching bgfx's texture-upload origin).
 *
 * The result is bit-identical to the `AreaTex` header bundled with the original
 * SMAA implementation (generated with the subsampling / compatible-16 /
 * numeric-diagonal / original-u-pattern options), so the ported reference
 * shaders sample exactly the coverage the algorithm was designed around. This
 * is a pure, backend-free computation — no bgfx, no GPU — so it can run off the
 * render thread and be validated in isolation.
 *
 * @param outRg8 receives kAreaTexWidth * kAreaTexHeight * 2 bytes.
 */
void BuildAreaTex(std::vector<std::uint8_t>& outRg8);

} // namespace Smaa
} // namespace Concord

#endif // CONCORD_SMAAAREATEX_H
