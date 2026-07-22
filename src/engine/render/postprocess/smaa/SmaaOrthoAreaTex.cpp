/**
 * SPDX-License-Identifier: MIT
 * Reference AreaTex algorithm from smaa-cpp by IRIE Shinsuke (2016-2017),
 * itself based on the original SMAA AreaTex generator.
 */
#include "engine/render/postprocess/smaa/SmaaAreaTexGenerators.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace Concord::Smaa::Detail {

namespace {

constexpr std::array<Int2, 16> kOrthoPatternOffsets = {
    Int2{0, 0}, Int2{0, 1}, Int2{0, 3}, Int2{0, 4},
    Int2{1, 0}, Int2{1, 1}, Int2{1, 3}, Int2{1, 4},
    Int2{3, 0}, Int2{3, 1}, Int2{3, 3}, Int2{3, 4},
    Int2{4, 0}, Int2{4, 1}, Int2{4, 3}, Int2{4, 4},
};

} // namespace

Double2 OrthoGenerator::SmoothArea(double distance, Double2 first, Double2 second) const
{
    const Double2 firstCurve{std::sqrt(first.x * 2.0) * 0.5, std::sqrt(first.y * 2.0) * 0.5};
    const Double2 secondCurve{std::sqrt(second.x * 2.0) * 0.5, std::sqrt(second.y * 2.0) * 0.5};
    const double amount = Saturate(distance / static_cast<double>(kSmoothMaxDistance));
    return Lerp(firstCurve, first, amount) + Lerp(secondCurve, second, amount);
}

Double2 OrthoGenerator::Area(Double2 first, Double2 second, int x) const
{
    const Double2 delta = second - first;
    const double x1 = static_cast<double>(x);
    const double x2 = x1 + 1.0;
    if (!((x1 >= first.x && x1 < second.x) || (x2 > first.x && x2 <= second.x))) {
        return {};
    }

    const double y1 = first.y + (x1 - first.x) * delta.y / delta.x;
    const double y2 = first.y + (x2 - first.x) * delta.y / delta.x;
    if (std::copysign(1.0, y1) == std::copysign(1.0, y2)
        || std::abs(y1) < 1.0e-4 || std::abs(y2) < 1.0e-4) {
        const double area = (y1 + y2) * 0.5;
        return area < 0.0 ? Double2{std::abs(area), 0.0} : Double2{0.0, std::abs(area)};
    }

    const double crossing = first.x - first.y * delta.x / delta.y;
    double integral = 0.0;
    const double fraction = std::modf(crossing, &integral);
    const double firstArea = crossing > first.x ? y1 * fraction * 0.5 : 0.0;
    const double secondArea = crossing < second.x ? y2 * (1.0 - fraction) * 0.5 : 0.0;
    const double dominant = std::abs(firstArea) > std::abs(secondArea) ? firstArea : -secondArea;
    return dominant < 0.0
        ? Double2{std::abs(firstArea), std::abs(secondArea)}
        : Double2{std::abs(secondArea), std::abs(firstArea)};
}

Double2 OrthoGenerator::Calculate(int pattern, int left, int right, double offset) const
{
    const double distance = static_cast<double>(left + right + 1);
    const double upper = 0.5 + offset;
    const double lower = upper - 1.0;
    Double2 first;
    Double2 second;

    switch (pattern) {
        case 0:
        case 3:
        case 12:
        case 15:
            return {};
        case 8:
            return left <= right ? Area({0.0, lower}, {distance * 0.5, 0.0}, left) : Double2{};
        case 2:
            return left >= right ? Area({distance * 0.5, 0.0}, {distance, lower}, left) : Double2{};
        case 10:
            first = Area({0.0, lower}, {distance * 0.5, 0.0}, left);
            second = Area({distance * 0.5, 0.0}, {distance, lower}, left);
            return SmoothArea(distance, first, second);
        case 4:
            return left <= right ? Area({0.0, upper}, {distance * 0.5, 0.0}, left) : Double2{};
        case 1:
            return left >= right ? Area({distance * 0.5, 0.0}, {distance, upper}, left) : Double2{};
        case 6:
            first = Area({0.0, upper}, {distance, lower}, left);
            if (std::abs(offset) == 0.0) return first;
            second = Area({0.0, upper}, {distance * 0.5, 0.0}, left);
            second += Area({distance * 0.5, 0.0}, {distance, lower}, left);
            return (first + second) / Double2(2.0);
        case 14:
        case 7:
            return Area({0.0, upper}, {distance, lower}, left);
        case 9:
            first = Area({0.0, lower}, {distance, upper}, left);
            if (std::abs(offset) == 0.0) return first;
            second = Area({0.0, lower}, {distance * 0.5, 0.0}, left);
            second += Area({distance * 0.5, 0.0}, {distance, upper}, left);
            return (first + second) / Double2(2.0);
        case 11:
        case 13:
            return Area({0.0, lower}, {distance, upper}, left);
        case 5:
            first = Area({0.0, upper}, {distance * 0.5, 0.0}, left);
            second = Area({distance * 0.5, 0.0}, {distance, upper}, left);
            return SmoothArea(distance, first, second);
        default:
            return {};
    }
}

void OrthoGenerator::Generate(double offset)
{
    std::fill(m_pixels.begin(), m_pixels.end(), Double2{});
    for (int pattern = 0; pattern < 16; ++pattern) {
        const Int2 base = Int2(kMaxDistanceOrtho) * kOrthoPatternOffsets[pattern];
        for (int left = 0; left < kMaxDistanceOrtho; ++left) {
            for (int right = 0; right < kMaxDistanceOrtho; ++right) {
                const Int2 coordinate = base + Int2(left, right);
                m_pixels[static_cast<std::size_t>(coordinate.y * kTextureSize + coordinate.x)] =
                    Calculate(pattern, left * left, right * right, offset);
            }
        }
    }
}

} // namespace Concord::Smaa::Detail
