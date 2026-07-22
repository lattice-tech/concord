#ifndef CONCORD_SMAAAREATEXGENERATORS_H
#define CONCORD_SMAAAREATEXGENERATORS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Concord::Smaa::Detail {

constexpr int kSubsamplesOrtho = 7;
constexpr int kSubsamplesDiag = 5;
constexpr int kMaxDistanceOrtho = 16;
constexpr int kMaxDistanceDiag = 20;
constexpr int kTextureSize = 80;
constexpr int kDiagSamples = 30;
constexpr int kSmoothMaxDistance = 32;

/** 2D double vector with the component-wise arithmetic the AreaTex math needs. */
struct Double2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Double2() = default;
    constexpr explicit Double2(double value) : x(value), y(value) {}
    constexpr Double2(double xValue, double yValue) : x(xValue), y(yValue) {}

    Double2 operator+(Double2 other) const { return {x + other.x, y + other.y}; }
    Double2 operator-(Double2 other) const { return {x - other.x, y - other.y}; }
    Double2 operator*(Double2 other) const { return {x * other.x, y * other.y}; }
    Double2 operator/(Double2 other) const { return {x / other.x, y / other.y}; }
    Double2& operator+=(Double2 other) { x += other.x; y += other.y; return *this; }
    bool operator==(Double2 other) const { return x == other.x && y == other.y; }
};

/** 2D integer vector used to index pattern blocks in the AreaTex layout. */
struct Int2 {
    int x = 0;
    int y = 0;

    constexpr Int2() = default;
    constexpr explicit Int2(int value) : x(value), y(value) {}
    constexpr Int2(int xValue, int yValue) : x(xValue), y(yValue) {}

    Int2 operator+(Int2 other) const { return {x + other.x, y + other.y}; }
    Int2 operator*(Int2 other) const { return {x * other.x, y * other.y}; }
};

/** Clamps `value` to [0, 1]. */
inline double Saturate(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

/** Component-wise linear interpolation between `from` and `to`. */
inline Double2 Lerp(Double2 from, Double2 to, double amount)
{
    return from + (to - from) * Double2(amount);
}

/** Saturates then scales a coverage value to an 8-bit texel channel. */
inline std::uint8_t Quantize(double value)
{
    return static_cast<std::uint8_t>(Saturate(value) * 255.0);
}

/**
 * Generates the orthogonal (horizontal/vertical edge) coverage area table for
 * one subsample offset into the caller's 80x80 pixel buffer.
 */
class OrthoGenerator {
public:
    explicit OrthoGenerator(std::vector<Double2>& pixels) : m_pixels(pixels) {}

    void Generate(double offset);

private:
    Double2 SmoothArea(double distance, Double2 first, Double2 second) const;
    Double2 Area(Double2 first, Double2 second, int x) const;
    Double2 Calculate(int pattern, int left, int right, double offset) const;

    std::vector<Double2>& m_pixels;
};

/**
 * Generates the diagonal-edge coverage area table for one subsample offset into
 * the caller's 80x80 pixel buffer, by numerically sampling each pattern.
 */
class DiagGenerator {
public:
    explicit DiagGenerator(std::vector<Double2>& pixels) : m_pixels(pixels) {}

    void Generate(Double2 offset);

private:
    double SampleArea(Double2 first, Double2 second, Int2 pixel) const;
    Double2 Area(Double2 first, Double2 second, int left) const;
    Double2 Calculate(int pattern, int left, int right, Double2 offset) const;

    std::vector<Double2>& m_pixels;
};

} // namespace Concord::Smaa::Detail

#endif // CONCORD_SMAAAREATEXGENERATORS_H
