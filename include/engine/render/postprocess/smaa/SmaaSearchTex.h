#ifndef CONCORD_SMAASEARCHTEX_H
#define CONCORD_SMAASEARCHTEX_H

#include <cstdint>
#include <vector>

namespace Concord {
namespace Smaa {

constexpr int kSearchTexWidth = 64;
constexpr int kSearchTexHeight = 16;

/**
 * Builds the reference SMAA search lookup texture as tightly-packed R8 data.
 * @param outR8 receives kSearchTexWidth * kSearchTexHeight bytes.
 */
void BuildSearchTex(std::vector<std::uint8_t>& outR8);

} // namespace Smaa
} // namespace Concord

#endif // CONCORD_SMAASEARCHTEX_H
