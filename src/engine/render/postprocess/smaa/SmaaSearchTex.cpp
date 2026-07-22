/**
 * SPDX-License-Identifier: MIT
 * Reference SearchTex algorithm from SMAA by Jorge Jimenez et al. (2013).
 */
#include "engine/render/postprocess/smaa/SmaaSearchTex.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

struct Edges {
    int topLeft = 0;
    int topRight = 0;
    int bottomLeft = 0;
    int bottomRight = 0;
    bool valid = false;
};

std::array<Edges, 33> BuildEdgeLookup()
{
    std::array<Edges, 33> lookup{};
    for (int bits = 0; bits < 16; ++bits) {
        Edges edges{
            .topLeft = (bits >> 3) & 1,
            .topRight = (bits >> 2) & 1,
            .bottomLeft = (bits >> 1) & 1,
            .bottomRight = bits & 1,
            .valid = true,
        };
        const int bilinearIndex = edges.topLeft + 3 * edges.topRight
            + 7 * edges.bottomLeft + 21 * edges.bottomRight;
        lookup[static_cast<std::size_t>(bilinearIndex)] = edges;
    }
    return lookup;
}

int DeltaLeft(const Edges& left, const Edges& top)
{
    int distance = top.bottomRight;
    if (distance == 1 && top.bottomLeft == 1
        && left.topRight != 1 && left.bottomRight != 1) {
        ++distance;
    }
    return distance;
}

int DeltaRight(const Edges& left, const Edges& top)
{
    int distance = top.bottomRight == 1
        && left.topRight != 1 && left.bottomRight != 1 ? 1 : 0;
    if (distance == 1 && top.bottomLeft == 1
        && left.topLeft != 1 && left.bottomLeft != 1) {
        ++distance;
    }
    return distance;
}

} // namespace

namespace Concord::Smaa {

void BuildSearchTex(std::vector<std::uint8_t>& outR8)
{
    static const std::array<Edges, 33> edgeLookup = BuildEdgeLookup();
    outR8.assign(static_cast<std::size_t>(kSearchTexWidth * kSearchTexHeight), 0);
    for (int y = 0; y < kSearchTexHeight; ++y) {
        const int topIndex = 32 - y;
        const Edges& top = edgeLookup[static_cast<std::size_t>(topIndex)];
        for (int x = 0; x < kSearchTexWidth; ++x) {
            const bool right = x >= 33;
            const int leftIndex = right ? x - 33 : x;
            const Edges& left = edgeLookup[static_cast<std::size_t>(leftIndex)];
            if (!left.valid || !top.valid) {
                continue;
            }
            const int distance = right ? DeltaRight(left, top) : DeltaLeft(left, top);
            outR8[static_cast<std::size_t>(y * kSearchTexWidth + x)] =
                static_cast<std::uint8_t>(127 * distance);
        }
    }
}

} // namespace Concord::Smaa
