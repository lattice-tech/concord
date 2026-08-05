#include "engine/animation/blend/detail/BlendSpace2DGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Concord::Animation::Detail {
namespace {

/** Coincident-point epsilon in blend-space units. */
constexpr double kCoincidentEpsilon = 1.0e-6;

struct P2d {
    double x = 0.0;
    double y = 0.0;
};

/** Twice the signed area of triangle (a, b, c); > 0 means counter-clockwise. */
double Cross(const P2d& a, const P2d& b, const P2d& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/**
 * Determinant test for `d` inside the circumcircle of counter-clockwise
 * (a, b, c). Sign of the incircle determinant: positive means inside.
 * Double precision keeps near-degenerate queries from flipping.
 */
bool InCircumcircle(const P2d& a, const P2d& b, const P2d& c, const P2d& d)
{
    const double ax = a.x - d.x;
    const double ay = a.y - d.y;
    const double bx = b.x - d.x;
    const double by = b.y - d.y;
    const double cx = c.x - d.x;
    const double cy = c.y - d.y;
    const double lhs = (ax * ax + ay * ay) * (bx * cy - cx * by)
        - (bx * bx + by * by) * (ax * cy - cx * ay)
        + (cx * cx + cy * cy) * (ax * by - bx * ay);
    return lhs > 0.0;
}

struct Edge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

bool SameEdge(const Edge& lhs, const Edge& rhs)
{
    return (lhs.a == rhs.a && lhs.b == rhs.b)
        || (lhs.a == rhs.b && lhs.b == rhs.a);
}

/** Appends an oriented edge; when its reverse already exists, both cancel. */
void MergeBoundary(std::vector<Edge>& boundary, std::uint32_t a, std::uint32_t b)
{
    const Edge candidate{a, b};
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        if (SameEdge(boundary[i], candidate)) {
            boundary.erase(boundary.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
    boundary.push_back(candidate);
}

/**
 * Super-triangle large enough to enclose every sample: centred on the
 * bounding box with an edge length of 64x the larger extent. The three
 * vertices are returned as (s0, s1, s2), counter-clockwise.
 */
void BuildSuperTriangle(const std::vector<P2d>& points, P2d& s0, P2d& s1,
                        P2d& s2)
{
    double minX = points[0].x;
    double maxX = minX;
    double minY = points[0].y;
    double maxY = minY;
    for (const P2d& p : points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double centreX = 0.5 * (minX + maxX);
    const double centreY = 0.5 * (minY + maxY);
    const double radius = 32.0 * std::max(maxX - minX, maxY - minY) + 1.0;
    s0 = P2d{centreX - radius, centreY - radius};
    s1 = P2d{centreX + radius, centreY - radius};
    s2 = P2d{centreX, centreY + radius};
}

/** Removes every triangle touching a super-triangle vertex. */
void StripSuperTriangle(std::vector<BlendTriangle>& triangles,
                        std::uint32_t s0, std::uint32_t s1, std::uint32_t s2)
{
    std::vector<BlendTriangle> kept;
    kept.reserve(triangles.size());
    for (const BlendTriangle& t : triangles) {
        if (t.a == s0 || t.a == s1 || t.a == s2
            || t.b == s0 || t.b == s1 || t.b == s2
            || t.c == s0 || t.c == s1 || t.c == s2) {
            continue;
        }
        kept.push_back(t);
    }
    triangles = std::move(kept);
}

} // namespace

bool BuildDelaunay(const std::vector<Vector2>& points,
                   std::vector<BlendTriangle>& outTriangles,
                   std::vector<std::uint32_t>& outRemap)
{
    outTriangles.clear();
    outRemap.clear();
    outRemap.resize(points.size());
    if (points.size() < 3) {
        return false;
    }

    // Deduplicate coincident points; the first occurrence wins.
    std::vector<P2d> unique;
    unique.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double x = static_cast<double>(points[i].x);
        const double y = static_cast<double>(points[i].y);
        std::uint32_t match = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t j = 0; j < unique.size(); ++j) {
            const double dx = unique[j].x - x;
            const double dy = unique[j].y - y;
            if (dx * dx + dy * dy <= kCoincidentEpsilon * kCoincidentEpsilon) {
                match = static_cast<std::uint32_t>(j);
                break;
            }
        }
        if (match == std::numeric_limits<std::uint32_t>::max()) {
            match = static_cast<std::uint32_t>(unique.size());
            unique.push_back(P2d{x, y});
        }
        outRemap[i] = match;
    }
    if (unique.size() < 3) {
        return false;
    }

    // Collinear detection: every triple has (near-)zero area.
    {
        bool collinear = true;
        for (std::size_t i = 0; i + 2 < unique.size(); ++i) {
            const double area = Cross(unique[i], unique[i + 1], unique[i + 2]);
            const double extent = std::max(
                std::max(std::fabs(unique[i].x), std::fabs(unique[i].y)),
                std::max(std::fabs(unique[i + 1].x), std::fabs(unique[i + 1].y)));
            if (std::fabs(area) > 1.0e-12 * (extent * extent + 1.0)) {
                collinear = false;
                break;
            }
        }
        if (collinear) {
            return false;
        }
    }

    P2d s0;
    P2d s1;
    P2d s2;
    BuildSuperTriangle(unique, s0, s1, s2);
    const std::uint32_t superCount = static_cast<std::uint32_t>(unique.size());
    unique.push_back(s0);
    unique.push_back(s1);
    unique.push_back(s2);
    const std::uint32_t t0 = superCount;
    const std::uint32_t t1 = superCount + 1;
    const std::uint32_t t2 = superCount + 2;

    std::vector<BlendTriangle> triangles;
    triangles.reserve(unique.size() * 2);
    triangles.push_back(BlendTriangle{t0, t1, t2});

    for (std::uint32_t i = 0; i < superCount; ++i) {
        const P2d& p = unique[i];

        std::vector<BlendTriangle> bad;
        bad.reserve(triangles.size());
        for (const BlendTriangle& t : triangles) {
            if (InCircumcircle(unique[t.a], unique[t.b], unique[t.c], p)) {
                bad.push_back(t);
            }
        }
        if (bad.empty()) {
            continue;
        }

        std::vector<Edge> boundary;
        boundary.reserve(bad.size() * 3);
        for (const BlendTriangle& t : bad) {
            MergeBoundary(boundary, t.a, t.b);
            MergeBoundary(boundary, t.b, t.c);
            MergeBoundary(boundary, t.c, t.a);
        }
        if (boundary.empty()) {
            continue;
        }

        std::vector<BlendTriangle> kept;
        kept.reserve(triangles.size() - bad.size() + boundary.size());
        for (const BlendTriangle& t : triangles) {
            bool wasBad = false;
            for (const BlendTriangle& b : bad) {
                if (t.a == b.a && t.b == b.b && t.c == b.c) {
                    wasBad = true;
                    break;
                }
            }
            if (!wasBad) {
                kept.push_back(t);
            }
        }
        for (const Edge& e : boundary) {
            kept.push_back(BlendTriangle{e.a, e.b, i});
        }
        triangles = std::move(kept);
    }

    StripSuperTriangle(triangles, t0, t1, t2);
    if (triangles.empty()) {
        return false;
    }
    outTriangles = std::move(triangles);
    return true;
}

bool BarycentricWeights(const Vector2& point, const BlendTriangle& triangle,
                        const std::vector<Vector2>& points, float& outA,
                        float& outB, float& outC)
{
    const P2d a{points[triangle.a].x, points[triangle.a].y};
    const P2d b{points[triangle.b].x, points[triangle.b].y};
    const P2d c{points[triangle.c].x, points[triangle.c].y};
    const P2d p{point.x, point.y};

    const double total = Cross(a, b, c);
    if (std::fabs(total) < 1.0e-15) {
        return false;
    }
    const double wA = Cross(p, b, c) / total;
    const double wB = Cross(a, p, c) / total;
    const double wC = Cross(a, b, p) / total;

    // Normalise away any tiny signed drift so the weights always sum to 1.
    const double sum = wA + wB + wC;
    const double inv = sum != 0.0 ? 1.0 / sum : 0.0;
    outA = static_cast<float>(wA * inv);
    outB = static_cast<float>(wB * inv);
    outC = static_cast<float>(wC * inv);
    return true;
}

bool ContainsPoint(const Vector2& point, const BlendTriangle& triangle,
                   const std::vector<Vector2>& points) noexcept
{
    const P2d a{points[triangle.a].x, points[triangle.a].y};
    const P2d b{points[triangle.b].x, points[triangle.b].y};
    const P2d c{points[triangle.c].x, points[triangle.c].y};
    const P2d p{point.x, point.y};
    const double total = Cross(a, b, c);
    if (std::fabs(total) < 1.0e-15) {
        return false;
    }
    const double wA = Cross(p, b, c) / total;
    const double wB = Cross(a, p, c) / total;
    const double wC = Cross(a, b, p) / total;
    return wA >= 0.0 && wB >= 0.0 && wC >= 0.0;
}

std::vector<std::uint32_t> BuildDedupToEntry(
    const std::vector<std::uint32_t>& remap, std::size_t dedupCount)
{
    std::vector<std::uint32_t> result(dedupCount,
                                      std::numeric_limits<std::uint32_t>::max());
    for (std::size_t i = 0; i < remap.size(); ++i) {
        const std::uint32_t dedup = remap[i];
        if (result[dedup] == std::numeric_limits<std::uint32_t>::max()) {
            result[dedup] = static_cast<std::uint32_t>(i);
        }
    }
    return result;
}

CollinearAxis BuildCollinearAxis(const std::vector<Vector2>& points)
{
    CollinearAxis axis;
    if (points.size() < 2) {
        return axis;
    }

    // Longest point pair defines the line direction.
    std::uint32_t farA = 0;
    std::uint32_t farB = 0;
    double best = -1.0;
    for (std::uint32_t i = 0; i < points.size(); ++i) {
        for (std::uint32_t j = i + 1; j < points.size(); ++j) {
            const double dx = static_cast<double>(points[i].x) - points[j].x;
            const double dy = static_cast<double>(points[i].y) - points[j].y;
            const double d2 = dx * dx + dy * dy;
            if (d2 > best) {
                best = d2;
                farA = i;
                farB = j;
            }
        }
    }
    const double dx = static_cast<double>(points[farB].x - points[farA].x);
    const double dy = static_cast<double>(points[farB].y - points[farA].y);
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0e-12) {
        return axis;
    }
    axis.direction = Vector2{
        static_cast<float>(dx / length),
        static_cast<float>(dy / length),
    };

    std::vector<std::pair<float, std::uint32_t>> ordered;
    ordered.reserve(points.size());
    for (std::uint32_t i = 0; i < points.size(); ++i) {
        const float t = (points[i].x - points[farA].x) * axis.direction.x
            + (points[i].y - points[farA].y) * axis.direction.y;
        ordered.emplace_back(t, i);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<float, std::uint32_t>& lhs,
                 const std::pair<float, std::uint32_t>& rhs) {
                  if (lhs.first != rhs.first) {
                      return lhs.first < rhs.first;
                  }
                  return lhs.second < rhs.second;
              });
    axis.parameters.reserve(ordered.size());
    axis.order.reserve(ordered.size());
    for (const auto& [t, index] : ordered) {
        axis.parameters.push_back(t);
        axis.order.push_back(index);
    }
    return axis;
}

} // namespace Concord::Animation::Detail
