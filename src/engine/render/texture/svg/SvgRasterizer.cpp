#include "engine/render/texture/svg/SvgRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Concord::Render::Svg {

namespace {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct PathSegment {
    Point a;
    Point b;
};

using Contour = std::vector<Point>;

bool IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',';
}

void SkipSpace(const std::string& text, std::size_t& index) noexcept
{
    while (index < text.size() && IsSpace(text[index])) {
        ++index;
    }
}

std::optional<std::string> Attribute(const std::string& text, const std::string& name)
{
    const std::string token = name + "=\"";
    const std::size_t start = text.find(token);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t valueStart = start + token.size();
    const std::size_t end = text.find('"', valueStart);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return text.substr(valueStart, end - valueStart);
}

float ParseFloat(const std::string& text, float fallback = 0.0f) noexcept
{
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    return end != text.c_str() ? value : fallback;
}

std::optional<std::uint32_t> ParseColor(const std::string& text)
{
    if (text.size() != 7 || text[0] != '#') {
        return std::nullopt;
    }
    const unsigned long value = std::strtoul(text.c_str() + 1, nullptr, 16);
    return (static_cast<std::uint32_t>(value) << 8) | 0xffu;
}

void PutPixel(RasterImage& image, int x, int y, std::uint32_t color) noexcept
{
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= image.width
        || static_cast<std::uint32_t>(y) >= image.height) {
        return;
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width
        + static_cast<std::size_t>(x)) * 4u;
    image.pixels[offset + 0] = static_cast<std::uint8_t>((color >> 24) & 0xffu);
    image.pixels[offset + 1] = static_cast<std::uint8_t>((color >> 16) & 0xffu);
    image.pixels[offset + 2] = static_cast<std::uint8_t>((color >> 8) & 0xffu);
    image.pixels[offset + 3] = static_cast<std::uint8_t>(color & 0xffu);
}

void FillRect(RasterImage& image, float x, float y, float w, float h, std::uint32_t color)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = static_cast<int>(std::ceil(x + w));
    const int y1 = static_cast<int>(std::ceil(y + h));
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            PutPixel(image, px, py, color);
        }
    }
}

void FillCircle(RasterImage& image, float cx, float cy, float radius, std::uint32_t color)
{
    const int x0 = static_cast<int>(std::floor(cx - radius));
    const int y0 = static_cast<int>(std::floor(cy - radius));
    const int x1 = static_cast<int>(std::ceil(cx + radius));
    const int y1 = static_cast<int>(std::ceil(cy + radius));
    const float r2 = radius * radius;
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            const float dx = (static_cast<float>(px) + 0.5f) - cx;
            const float dy = (static_cast<float>(py) + 0.5f) - cy;
            if (dx * dx + dy * dy <= r2) {
                PutPixel(image, px, py, color);
            }
        }
    }
}

void StrokeSegment(RasterImage& image, Point a, Point b, float thickness, std::uint32_t color)
{
    const float minX = std::min(a.x, b.x) - thickness;
    const float maxX = std::max(a.x, b.x) + thickness;
    const float minY = std::min(a.y, b.y) - thickness;
    const float maxY = std::max(a.y, b.y) + thickness;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float radius = thickness * 0.5f;
    for (int py = static_cast<int>(std::floor(minY)); py <= static_cast<int>(std::ceil(maxY)); ++py) {
        for (int px = static_cast<int>(std::floor(minX)); px <= static_cast<int>(std::ceil(maxX)); ++px) {
            const float sampleX = static_cast<float>(px) + 0.5f;
            const float sampleY = static_cast<float>(py) + 0.5f;
            float t = 0.0f;
            if (lengthSquared > 0.0f) {
                t = ((sampleX - a.x) * dx + (sampleY - a.y) * dy) / lengthSquared;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            const float closestX = a.x + dx * t;
            const float closestY = a.y + dy * t;
            const float distX = sampleX - closestX;
            const float distY = sampleY - closestY;
            if (distX * distX + distY * distY <= radius * radius) {
                PutPixel(image, px, py, color);
            }
        }
    }
}

bool IsCommand(char c) noexcept
{
    return c == 'M' || c == 'L' || c == 'Z' || c == 'm' || c == 'l' || c == 'z';
}

std::optional<float> ParseNumber(const std::string& text, std::size_t& index)
{
    SkipSpace(text, index);
    const std::size_t start = index;
    if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
        ++index;
    }
    bool sawDigit = false;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        sawDigit = true;
        ++index;
    }
    if (index < text.size() && text[index] == '.') {
        ++index;
        while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
            sawDigit = true;
            ++index;
        }
    }
    if (!sawDigit) {
        index = start;
        return std::nullopt;
    }
    return ParseFloat(text.substr(start, index - start));
}

std::vector<Contour> ParseContours(const std::string& d)
{
    std::vector<Contour> contours;
    std::size_t index = 0;
    char command = 0;
    Point cursor{};
    Point subpathStart{};
    bool hasCursor = false;
    Contour contour;

    const auto flushContour = [&]() {
        if (contour.size() >= 2) {
            contours.push_back(contour);
        }
        contour.clear();
    };

    while (index < d.size()) {
        SkipSpace(d, index);
        if (index >= d.size()) {
            break;
        }
        if (IsCommand(d[index])) {
            command = d[index++];
            if (command == 'Z' || command == 'z') {
                if (hasCursor) {
                    contour.push_back(subpathStart);
                    cursor = subpathStart;
                }
                flushContour();
                hasCursor = false;
                continue;
            }
        }
        if (command == 0) {
            continue;
        }
        const std::optional<float> x = ParseNumber(d, index);
        const std::optional<float> y = ParseNumber(d, index);
        if (!x || !y) {
            ++index;
            continue;
        }
        Point point{*x, *y};
        if (command == 'm' || command == 'l') {
            point.x += cursor.x;
            point.y += cursor.y;
        }
        if (command == 'H') {
            point = Point{*x, cursor.y};
        } else if (command == 'h') {
            point = Point{cursor.x + *x, cursor.y};
        } else if (command == 'V') {
            point = Point{cursor.x, *x};
        } else if (command == 'v') {
            point = Point{cursor.x, cursor.y + *x};
        }
        if (command == 'M' || command == 'm') {
            flushContour();
            cursor = point;
            subpathStart = point;
            hasCursor = true;
            contour.push_back(point);
            command = command == 'm' ? 'l' : 'L';
            continue;
        }
        if (!hasCursor) {
            subpathStart = point;
            contour.push_back(point);
        }
        cursor = point;
        contour.push_back(point);
        hasCursor = true;
    }
    flushContour();
    return contours;
}

std::vector<PathSegment> BuildSegments(const std::vector<Contour>& contours)
{
    std::vector<PathSegment> segments;
    for (const Contour& contour : contours) {
        if (contour.size() < 2) {
            continue;
        }
        for (std::size_t index = 1; index < contour.size(); ++index) {
            segments.push_back(PathSegment{contour[index - 1], contour[index]});
        }
    }
    return segments;
}

bool PointInContour(const Contour& contour, float x, float y) noexcept
{
    bool inside = false;
    if (contour.size() < 3) {
        return false;
    }
    for (std::size_t i = 0, j = contour.size() - 1; i < contour.size(); j = i++) {
        const Point& a = contour[i];
        const Point& b = contour[j];
        const bool intersects = ((a.y > y) != (b.y > y))
            && (x < (b.x - a.x) * (y - a.y) / ((b.y - a.y) == 0.0f ? 1e-6f : (b.y - a.y)) + a.x);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

void FillContours(RasterImage& image, const std::vector<Contour>& contours, std::uint32_t color)
{
    if (contours.empty()) {
        return;
    }
    float minX = static_cast<float>(image.width);
    float minY = static_cast<float>(image.height);
    float maxX = 0.0f;
    float maxY = 0.0f;
    for (const Contour& contour : contours) {
        for (const Point& point : contour) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }
    }
    for (int py = static_cast<int>(std::floor(minY)); py <= static_cast<int>(std::ceil(maxY)); ++py) {
        for (int px = static_cast<int>(std::floor(minX)); px <= static_cast<int>(std::ceil(maxX)); ++px) {
            const float sampleX = static_cast<float>(px) + 0.5f;
            const float sampleY = static_cast<float>(py) + 0.5f;
            bool inside = false;
            for (const Contour& contour : contours) {
                if (PointInContour(contour, sampleX, sampleY)) {
                    inside = !inside;
                }
            }
            if (inside) {
                PutPixel(image, px, py, color);
            }
        }
    }
}

void RasterizeRect(RasterImage& image, const std::string& element)
{
    const float x = ParseFloat(Attribute(element, "x").value_or("0"));
    const float y = ParseFloat(Attribute(element, "y").value_or("0"));
    const float width = ParseFloat(Attribute(element, "width").value_or("0"));
    const float height = ParseFloat(Attribute(element, "height").value_or("0"));
    const std::uint32_t color = ParseColor(Attribute(element, "fill").value_or("#ffffff")).value_or(0xffffffffu);
    FillRect(image, x, y, width, height, color);
}

void RasterizePath(RasterImage& image, const std::string& element)
{
    const std::optional<std::string> d = Attribute(element, "d");
    if (!d) {
        return;
    }
    const std::vector<Contour> contours = ParseContours(*d);

    const std::optional<std::string> fill = Attribute(element, "fill");
    if (fill && *fill != "none") {
        const std::uint32_t fillColor = *fill == "currentColor"
            ? 0xffffffffu
            : ParseColor(*fill).value_or(0xffffffffu);
        FillContours(image, contours, fillColor);
    }

    const std::optional<std::string> stroke = Attribute(element, "stroke");
    if (stroke && *stroke != "none") {
        const float thickness = ParseFloat(Attribute(element, "stroke-width").value_or("1"), 1.0f);
        const std::uint32_t strokeColor = *stroke == "currentColor"
            ? 0xffffffffu
            : ParseColor(*stroke).value_or(0xffffffffu);
        const std::vector<PathSegment> segments = BuildSegments(contours);
        for (const PathSegment& segment : segments) {
            StrokeSegment(image, segment.a, segment.b, thickness, strokeColor);
        }
        if (Attribute(element, "stroke-linecap").value_or("") == "round") {
            const float radius = thickness * 0.5f;
            for (const PathSegment& segment : segments) {
                FillCircle(image, segment.a.x, segment.a.y, radius, strokeColor);
                FillCircle(image, segment.b.x, segment.b.y, radius, strokeColor);
            }
        }
    }
}

} // namespace

std::optional<RasterImage> Rasterize(const std::string& path,
                                     const std::vector<std::uint8_t>& bytes)
{
    (void)path;
    const std::string text(bytes.begin(), bytes.end());
    const float width = ParseFloat(Attribute(text, "width").value_or("16"), 16.0f);
    const float height = ParseFloat(Attribute(text, "height").value_or("16"), 16.0f);
    if (!(width > 0.0f && height > 0.0f)
        || width > static_cast<float>(std::numeric_limits<std::uint16_t>::max())
        || height > static_cast<float>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }

    RasterImage image;
    image.width = static_cast<std::uint32_t>(std::ceil(width));
    image.height = static_cast<std::uint32_t>(std::ceil(height));
    image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4u, 0u);

    std::size_t start = 0;
    while ((start = text.find("<rect", start)) != std::string::npos) {
        const std::size_t end = text.find('>', start);
        if (end == std::string::npos) {
            break;
        }
        RasterizeRect(image, text.substr(start, end - start + 1));
        start = end + 1;
    }

    start = 0;
    while ((start = text.find("<path", start)) != std::string::npos) {
        const std::size_t end = text.find('>', start);
        if (end == std::string::npos) {
            break;
        }
        RasterizePath(image, text.substr(start, end - start + 1));
        start = end + 1;
    }

    return image;
}

} // namespace Concord::Render::Svg
