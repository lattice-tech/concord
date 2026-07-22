#include "engine/render/mesh/Primitives.h"

#include <cmath>
#include <cstdint>

namespace Concord::Primitives {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 Normalize(const Vector3& v) noexcept
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

/** Appends one vertex (position + outward normal + texture coordinate) and returns its index. */
std::uint16_t AddVertex(MeshData& mesh, const Vector3& position, const Vector3& normal, const Vector2& uv)
{
    const auto index = static_cast<std::uint16_t>(mesh.positions.size());
    mesh.positions.push_back(position);
    mesh.normals.push_back(normal);
    mesh.uvs.push_back(uv);
    return index;
}

/**
 * Appends one triangle.
 *
 * Every generator below winds its triangles counter-clockwise when viewed
 * from outside the solid (i.e. `cross(b - a, c - a)` points along the outward
 * normal). This single, vetted convention is what lets the backend enable
 * back-face culling; any generator that breaks it would drop its front faces.
 */
void PushTriangle(MeshData& mesh, std::uint16_t a, std::uint16_t b, std::uint16_t c)
{
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

/**
 * Appends a flat quad (a,b,c,d) sharing one face normal, as two triangles.
 * The corners map to the full [0,1] UV square (a=00, b=10, c=11, d=01) so a
 * texture lands once per face.
 */
void AddQuad(MeshData& mesh,
             const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
             const Vector3& normal)
{
    const std::uint16_t i0 = AddVertex(mesh, a, normal, {0.0f, 0.0f});
    const std::uint16_t i1 = AddVertex(mesh, b, normal, {1.0f, 0.0f});
    const std::uint16_t i2 = AddVertex(mesh, c, normal, {1.0f, 1.0f});
    const std::uint16_t i3 = AddVertex(mesh, d, normal, {0.0f, 1.0f});
    PushTriangle(mesh, i0, i1, i2);
    PushTriangle(mesh, i0, i2, i3);
}

} // namespace

MeshData UnitCube()
{
    // Six independent faces (24 vertices) so each carries its own flat normal;
    // a shared-corner cube could not express per-face normals for lighting.
    MeshData mesh;
    AddQuad(mesh, {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}, { 0,  0,  1});
    AddQuad(mesh, { 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}, { 0,  0, -1});
    AddQuad(mesh, { 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}, { 1,  0,  0});
    AddQuad(mesh, {-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}, {-1,  0,  0});
    AddQuad(mesh, {-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}, { 0,  1,  0});
    AddQuad(mesh, {-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}, { 0, -1,  0});
    return mesh;
}

MeshData UnitQuad()
{
    MeshData mesh;
    AddQuad(mesh, {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {0, 0, 1});
    return mesh;
}

MeshData Sphere(int rings, int segments)
{
    if (rings < 2) {
        rings = 2;
    }
    if (segments < 3) {
        segments = 3;
    }

    MeshData mesh;
    // On a unit sphere the outward normal equals the position, so each sample
    // supplies both at once.
    for (int r = 0; r <= rings; ++r) {
        const float phi = static_cast<float>(r) / static_cast<float>(rings) * kPi;
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);
        const float v = static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const Vector3 p{ringRadius * std::cos(theta), y, ringRadius * std::sin(theta)};
            AddVertex(mesh, p, Normalize(p), {u, v});
        }
    }

    const int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const auto i0 = static_cast<std::uint16_t>(r * stride + s);
            const auto i1 = static_cast<std::uint16_t>(i0 + 1);
            const auto i2 = static_cast<std::uint16_t>(i0 + stride);
            const auto i3 = static_cast<std::uint16_t>(i2 + 1);
            // CCW seen from outside: rings descend in y as r grows, so the
            // outward-facing order is (i0, i1, i2) / (i1, i3, i2).
            PushTriangle(mesh, i0, i1, i2);
            PushTriangle(mesh, i1, i3, i2);
        }
    }
    return mesh;
}

MeshData Cylinder(int segments)
{
    if (segments < 3) {
        segments = 3;
    }

    MeshData mesh;
    // Side wall: radial normals (no y component) so it shades like a tube. U
    // wraps 0..1 around the axis, V runs 0 (bottom) to 1 (top).
    for (int s = 0; s < segments; ++s) {
        const float u0 = static_cast<float>(s) / static_cast<float>(segments);
        const float u1 = static_cast<float>(s + 1) / static_cast<float>(segments);
        const float t0 = u0 * 2.0f * kPi;
        const float t1 = u1 * 2.0f * kPi;
        const Vector3 n0{std::cos(t0), 0.0f, std::sin(t0)};
        const Vector3 n1{std::cos(t1), 0.0f, std::sin(t1)};
        const std::uint16_t top0 = AddVertex(mesh, {n0.x, 1.0f, n0.z}, n0, {u0, 1.0f});
        const std::uint16_t bot0 = AddVertex(mesh, {n0.x, -1.0f, n0.z}, n0, {u0, 0.0f});
        const std::uint16_t top1 = AddVertex(mesh, {n1.x, 1.0f, n1.z}, n1, {u1, 1.0f});
        const std::uint16_t bot1 = AddVertex(mesh, {n1.x, -1.0f, n1.z}, n1, {u1, 0.0f});
        PushTriangle(mesh, top0, top1, bot0);
        PushTriangle(mesh, top1, bot1, bot0);
    }

    // Flat caps, each a triangle fan around a center vertex. Caps map the
    // circle into the UV square (center at 0.5,0.5) as a planar projection.
    const std::uint16_t topCenter = AddVertex(mesh, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f});
    const std::uint16_t botCenter = AddVertex(mesh, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f});
    for (int s = 0; s < segments; ++s) {
        const float t0 = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * kPi;
        const float t1 = static_cast<float>(s + 1) / static_cast<float>(segments) * 2.0f * kPi;
        const Vector2 uv0{std::cos(t0) * 0.5f + 0.5f, std::sin(t0) * 0.5f + 0.5f};
        const Vector2 uv1{std::cos(t1) * 0.5f + 0.5f, std::sin(t1) * 0.5f + 0.5f};
        const std::uint16_t t0v = AddVertex(mesh, {std::cos(t0), 1.0f, std::sin(t0)}, {0.0f, 1.0f, 0.0f}, uv0);
        const std::uint16_t t1v = AddVertex(mesh, {std::cos(t1), 1.0f, std::sin(t1)}, {0.0f, 1.0f, 0.0f}, uv1);
        PushTriangle(mesh, topCenter, t1v, t0v);
        const std::uint16_t b0v = AddVertex(mesh, {std::cos(t0), -1.0f, std::sin(t0)}, {0.0f, -1.0f, 0.0f}, uv0);
        const std::uint16_t b1v = AddVertex(mesh, {std::cos(t1), -1.0f, std::sin(t1)}, {0.0f, -1.0f, 0.0f}, uv1);
        PushTriangle(mesh, botCenter, b0v, b1v);
    }
    return mesh;
}

MeshData Cone(int segments)
{
    if (segments < 3) {
        segments = 3;
    }

    MeshData mesh;
    // Side: the outward normal of a unit cone (base r=1 at y=-1, apex at y=1)
    // tilts up by half the radial component; per-segment vertices give it a
    // smooth wrap. The apex normal is the mean of its segment's base normals.
    for (int s = 0; s < segments; ++s) {
        const float u0 = static_cast<float>(s) / static_cast<float>(segments);
        const float u1 = static_cast<float>(s + 1) / static_cast<float>(segments);
        const float t0 = u0 * 2.0f * kPi;
        const float t1 = u1 * 2.0f * kPi;
        const Vector3 n0 = Normalize({std::cos(t0), 0.5f, std::sin(t0)});
        const Vector3 n1 = Normalize({std::cos(t1), 0.5f, std::sin(t1)});
        const Vector3 nApex = Normalize({n0.x + n1.x, n0.y + n1.y, n0.z + n1.z});
        const std::uint16_t apex = AddVertex(mesh, {0.0f, 1.0f, 0.0f}, nApex, {(u0 + u1) * 0.5f, 1.0f});
        const std::uint16_t base0 = AddVertex(mesh, {std::cos(t0), -1.0f, std::sin(t0)}, n0, {u0, 0.0f});
        const std::uint16_t base1 = AddVertex(mesh, {std::cos(t1), -1.0f, std::sin(t1)}, n1, {u1, 0.0f});
        PushTriangle(mesh, apex, base1, base0);
    }

    // Flat base cap, facing down (-Y). Wound (center, b0, b1) so its outward
    // normal points along -Y, matching the CCW-from-outside convention. UVs
    // project the circle into the UV square.
    const std::uint16_t center = AddVertex(mesh, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f});
    for (int s = 0; s < segments; ++s) {
        const float t0 = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * kPi;
        const float t1 = static_cast<float>(s + 1) / static_cast<float>(segments) * 2.0f * kPi;
        const Vector2 uv0{std::cos(t0) * 0.5f + 0.5f, std::sin(t0) * 0.5f + 0.5f};
        const Vector2 uv1{std::cos(t1) * 0.5f + 0.5f, std::sin(t1) * 0.5f + 0.5f};
        const std::uint16_t b0 = AddVertex(mesh, {std::cos(t0), -1.0f, std::sin(t0)}, {0.0f, -1.0f, 0.0f}, uv0);
        const std::uint16_t b1 = AddVertex(mesh, {std::cos(t1), -1.0f, std::sin(t1)}, {0.0f, -1.0f, 0.0f}, uv1);
        PushTriangle(mesh, center, b0, b1);
    }
    return mesh;
}

MeshData Capsule(int hemisphereRings, int segments)
{
    if (hemisphereRings < 2) {
        hemisphereRings = 2;
    }
    if (segments < 3) {
        segments = 3;
    }

    constexpr float kRadius = 0.5f;
    constexpr float kHalfCylinder = 0.5f;
    MeshData mesh;

    const auto addProfileRing = [&](float radial, float y, float normalRadial,
                                    float normalY) {
        const float v = (y + 1.0f) * 0.5f;
        for (int segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);
            AddVertex(mesh,
                      {radial * cosTheta, y, radial * sinTheta},
                      {normalRadial * cosTheta, normalY, normalRadial * sinTheta},
                      {u, v});
        }
    };

    for (int ring = 0; ring <= hemisphereRings; ++ring) {
        const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
        const float latitude = -kPi * 0.5f + t * kPi * 0.5f;
        addProfileRing(kRadius * std::cos(latitude),
                       -kHalfCylinder + kRadius * std::sin(latitude),
                       std::cos(latitude), std::sin(latitude));
    }
    for (int ring = 0; ring <= hemisphereRings; ++ring) {
        const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
        const float latitude = t * kPi * 0.5f;
        addProfileRing(kRadius * std::cos(latitude),
                       kHalfCylinder + kRadius * std::sin(latitude),
                       std::cos(latitude), std::sin(latitude));
    }

    const int profileRings = hemisphereRings * 2 + 2;
    const int stride = segments + 1;
    for (int ring = 0; ring + 1 < profileRings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const auto i0 = static_cast<std::uint16_t>(ring * stride + segment);
            const auto i1 = static_cast<std::uint16_t>(i0 + 1);
            const auto i2 = static_cast<std::uint16_t>(i0 + stride);
            const auto i3 = static_cast<std::uint16_t>(i2 + 1);
            PushTriangle(mesh, i0, i2, i1);
            PushTriangle(mesh, i1, i2, i3);
        }
    }
    return mesh;
}

MeshData Torus(int majorSegments, int minorSegments)
{
    if (majorSegments < 3) {
        majorSegments = 3;
    }
    if (minorSegments < 3) {
        minorSegments = 3;
    }

    constexpr float kMajorRadius = 0.7f;
    constexpr float kMinorRadius = 0.3f;
    MeshData mesh;
    for (int major = 0; major <= majorSegments; ++major) {
        const float u = static_cast<float>(major) / static_cast<float>(majorSegments);
        const float theta = u * 2.0f * kPi;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        for (int minor = 0; minor <= minorSegments; ++minor) {
            const float v = static_cast<float>(minor) / static_cast<float>(minorSegments);
            const float phi = v * 2.0f * kPi;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);
            const float ringRadius = kMajorRadius + kMinorRadius * cosPhi;
            AddVertex(mesh,
                      {ringRadius * cosTheta, kMinorRadius * sinPhi,
                       ringRadius * sinTheta},
                      {cosPhi * cosTheta, sinPhi, cosPhi * sinTheta},
                      {u, v});
        }
    }

    const int stride = minorSegments + 1;
    for (int major = 0; major < majorSegments; ++major) {
        for (int minor = 0; minor < minorSegments; ++minor) {
            const auto i0 = static_cast<std::uint16_t>(major * stride + minor);
            const auto i1 = static_cast<std::uint16_t>(i0 + stride);
            const auto i2 = static_cast<std::uint16_t>(i0 + 1);
            const auto i3 = static_cast<std::uint16_t>(i1 + 1);
            PushTriangle(mesh, i0, i2, i1);
            PushTriangle(mesh, i1, i2, i3);
        }
    }
    return mesh;
}

} // namespace Concord::Primitives
