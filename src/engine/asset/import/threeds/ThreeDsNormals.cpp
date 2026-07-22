#include "engine/asset/import/threeds/ThreeDsNormals.h"

#include <cmath>
#include <cstdint>

namespace Concord::Asset::ThreeDs {

namespace {

constexpr Vector3 kUp{0.0f, 1.0f, 0.0f};

/** Unnormalized triangle normal; its length is twice the triangle area. */
Vector3 RawFaceNormal(const Vector3& a, const Vector3& b, const Vector3& c) noexcept
{
    const Vector3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vector3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
    return {e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x};
}

float Length(const Vector3& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 Normalized(const Vector3& v) noexcept
{
    const float len = Length(v);
    if (len <= 1e-12f) {
        return kUp;
    }
    return {v.x / len, v.y / len, v.z / len};
}

float Dot(const Vector3& a, const Vector3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

std::vector<std::array<Vector3, 3>> GenerateCornerNormals(
    const std::vector<Vector3>& positions,
    const std::vector<ParsedMesh::Face>& faces,
    float creaseDegrees)
{
    std::vector<std::array<Vector3, 3>> corners(faces.size(),
                                                {kUp, kUp, kUp});
    if (positions.empty() || faces.empty()) {
        return corners;
    }

    // Area-weighted (raw) and unit facet normals for every face; degenerate
    // faces are flagged so they neither contribute to nor query the average.
    std::vector<Vector3> rawNormals(faces.size(), Vector3{0.0f, 0.0f, 0.0f});
    std::vector<Vector3> unitNormals(faces.size(), kUp);
    std::vector<bool> valid(faces.size(), false);

    // Vertex -> incident faces, so each corner only averages its own neighbours.
    std::vector<std::vector<std::uint32_t>> incident(positions.size());

    for (std::size_t f = 0; f < faces.size(); ++f) {
        const ParsedMesh::Face& face = faces[f];
        if (face.v[0] >= positions.size() || face.v[1] >= positions.size()
            || face.v[2] >= positions.size()) {
            continue;
        }
        const Vector3 raw = RawFaceNormal(positions[face.v[0]], positions[face.v[1]],
                                          positions[face.v[2]]);
        if (Length(raw) <= 1e-12f) {
            continue; // zero-area triangle
        }
        rawNormals[f] = raw;
        unitNormals[f] = Normalized(raw);
        valid[f] = true;
        for (int i = 0; i < 3; ++i) {
            incident[face.v[i]].push_back(static_cast<std::uint32_t>(f));
        }
    }

    const float cosCrease = std::cos(creaseDegrees * 3.14159265358979323846f / 180.0f);

    for (std::size_t f = 0; f < faces.size(); ++f) {
        if (!valid[f]) {
            continue;
        }
        const ParsedMesh::Face& face = faces[f];
        for (int i = 0; i < 3; ++i) {
            const std::uint32_t vertex = face.v[i];
            Vector3 acc{0.0f, 0.0f, 0.0f};
            for (const std::uint32_t g : incident[vertex]) {
                if (Dot(unitNormals[f], unitNormals[g]) >= cosCrease) {
                    acc.x += rawNormals[g].x;
                    acc.y += rawNormals[g].y;
                    acc.z += rawNormals[g].z;
                }
            }
            corners[f][i] = (Length(acc) > 1e-12f) ? Normalized(acc) : unitNormals[f];
        }
    }
    return corners;
}

} // namespace Concord::Asset::ThreeDs
