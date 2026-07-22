/**
 * SPDX-License-Identifier: MIT
 * Reference AreaTex algorithm from smaa-cpp by IRIE Shinsuke (2016-2017),
 * itself based on the original SMAA AreaTex generator.
 */
#include "engine/render/postprocess/smaa/SmaaAreaTexGenerators.h"

#include <array>
#include <cstddef>

namespace Concord::Smaa::Detail {

namespace {

constexpr std::array<Int2, 16> kDiagPatternOffsets = {
    Int2{0, 0}, Int2{0, 1}, Int2{0, 2}, Int2{0, 3},
    Int2{1, 0}, Int2{1, 1}, Int2{1, 2}, Int2{1, 3},
    Int2{2, 0}, Int2{2, 1}, Int2{2, 2}, Int2{2, 3},
    Int2{3, 0}, Int2{3, 1}, Int2{3, 2}, Int2{3, 3},
};

} // namespace

double DiagGenerator::SampleArea(Double2 first, Double2 second, Int2 pixel) const
{
    if (first == second) {
        return 1.0;
    }
    const double middleX = (first.x + second.x) * 0.5;
    const double middleY = (first.y + second.y) * 0.5;
    const double lineA = second.y - first.y;
    const double lineB = first.x - second.x;
    int count = 0;
    for (int x = 0; x < kDiagSamples; ++x) {
        const double sampleX = pixel.x + static_cast<double>(x) / (kDiagSamples - 1);
        for (int y = 0; y < kDiagSamples; ++y) {
            const double sampleY = pixel.y + static_cast<double>(y) / (kDiagSamples - 1);
            if (lineA * (sampleX - middleX) + lineB * (sampleY - middleY) > 0.0) {
                ++count;
            }
        }
    }
    return static_cast<double>(count) / static_cast<double>(kDiagSamples * kDiagSamples);
}

Double2 DiagGenerator::Area(Double2 first, Double2 second, int left) const
{
    const double firstArea = SampleArea(first, second, Int2(1 + left, left));
    const double secondArea = SampleArea(first, second, Int2(1 + left, 1 + left));
    return {1.0 - firstArea, secondArea};
}

Double2 DiagGenerator::Calculate(int pattern, int left, int right, Double2 offset) const
{
    const double distance = static_cast<double>(left + right + 1);
    const Double2 distance2(distance);
    Double2 first;
    Double2 second;

    switch (pattern) {
        case 0:
            first = Area({1.0, 1.0}, Double2{1.0, 1.0} + distance2, left);
            second = Area({1.0, 0.0}, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 4:
            first = Area(Double2{1.0, 0.0} + offset, Double2{0.0, 0.0} + distance2, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 2:
            first = Area({0.0, 0.0}, Double2{1.0, 0.0} + distance2 + offset, left);
            second = Area({1.0, 0.0}, Double2{1.0, 0.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        case 6:
            return Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
        default:
            break;
    }
    switch (pattern) {
        case 8:
            first = Area(Double2{1.0, 1.0} + offset, Double2{0.0, 0.0} + distance2, left);
            second = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 12:
            first = Area(Double2{1.0, 1.0} + offset, Double2{0.0, 0.0} + distance2, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 10:
            return Area(Double2{1.0, 1.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
        case 14:
            first = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        case 1:
            first = Area({0.0, 0.0}, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area({1.0, 0.0}, Double2{1.0, 1.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        case 5:
            return Area(Double2{1.0, 0.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
        case 3:
            first = Area({0.0, 0.0}, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area({1.0, 0.0}, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 7:
            first = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2, left);
            return (first + second) / Double2(2.0);
        case 9:
            return Area(Double2{1.0, 1.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
        case 13:
            first = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        case 11:
            first = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        case 15:
            first = Area(Double2{1.0, 1.0} + offset, Double2{1.0, 1.0} + distance2 + offset, left);
            second = Area(Double2{1.0, 0.0} + offset, Double2{1.0, 0.0} + distance2 + offset, left);
            return (first + second) / Double2(2.0);
        default:
            return {};
    }
}

void DiagGenerator::Generate(Double2 offset)
{
    std::fill(m_pixels.begin(), m_pixels.end(), Double2{});
    for (int pattern = 0; pattern < 16; ++pattern) {
        const Int2 base = Int2(kMaxDistanceDiag) * kDiagPatternOffsets[pattern];
        for (int left = 0; left < kMaxDistanceDiag; ++left) {
            for (int right = 0; right < kMaxDistanceDiag; ++right) {
                const Int2 coordinate = base + Int2(left, right);
                m_pixels[static_cast<std::size_t>(coordinate.y * kTextureSize + coordinate.x)] =
                    Calculate(pattern, left, right, offset);
            }
        }
    }
}

} // namespace Concord::Smaa::Detail
