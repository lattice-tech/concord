#include "engine/asset/import/threeds/ThreeDsTransform.h"

#include <algorithm>
#include <cmath>

namespace Concord::Asset::ThreeDs {

namespace {

float Length3(float x, float y, float z) noexcept
{
    return std::sqrt(x * x + y * y + z * z);
}

void Normalize3(float& x, float& y, float& z) noexcept
{
    const float len = Length3(x, y, z);
    if (len <= 1e-8f) {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

struct Aabb {
    Vector3 bmin{};
    Vector3 bmax{};
    bool valid = false;
};

void Expand(Aabb& box, const Vector3& p) noexcept
{
    if (!box.valid) {
        box.bmin = box.bmax = p;
        box.valid = true;
        return;
    }
    box.bmin.x = std::min(box.bmin.x, p.x);
    box.bmin.y = std::min(box.bmin.y, p.y);
    box.bmin.z = std::min(box.bmin.z, p.z);
    box.bmax.x = std::max(box.bmax.x, p.x);
    box.bmax.y = std::max(box.bmax.y, p.y);
    box.bmax.z = std::max(box.bmax.z, p.z);
}

Aabb BoundsOf(const std::vector<Vector3>& positions) noexcept
{
    Aabb box;
    for (const Vector3& p : positions) {
        Expand(box, p);
    }
    return box;
}

float MinExtent(const Aabb& box) noexcept
{
    if (!box.valid) {
        return 0.0f;
    }
    return std::min({box.bmax.x - box.bmin.x, box.bmax.y - box.bmin.y, box.bmax.z - box.bmin.z});
}

float MaxExtent(const Aabb& box) noexcept
{
    if (!box.valid) {
        return 0.0f;
    }
    return std::max({box.bmax.x - box.bmin.x, box.bmax.y - box.bmin.y, box.bmax.z - box.bmin.z});
}

/** Mean of each mesh's thinnest dimension, divided by the global max extent. */
float MeanAbsoluteThickness(const std::vector<PreparedPositions>& meshes) noexcept
{
    Aabb global;
    std::vector<Aabb> boxes;
    boxes.reserve(meshes.size());
    for (const PreparedPositions& mesh : meshes) {
        const Aabb box = BoundsOf(mesh.positions);
        boxes.push_back(box);
        if (box.valid) {
            Expand(global, box.bmin);
            Expand(global, box.bmax);
        }
    }
    if (!global.valid) {
        return 0.0f;
    }
    const float globalMax = std::max(MaxExtent(global), 1e-6f);
    float sum = 0.0f;
    int n = 0;
    for (const Aabb& box : boxes) {
        if (!box.valid) {
            continue;
        }
        sum += MinExtent(box) / globalMax;
        ++n;
    }
    return n > 0 ? sum / static_cast<float>(n) : 0.0f;
}

/**
 * True when a majority of mesh matrices carry a non-unit scale. That pattern is
 * the Max pivot / unit-conversion dialect: vertices are already in scene units
 * and baking the matrix shreds architecture into paper walls.
 */
bool MajorityNonUnitScale(const std::vector<std::array<float, 12>>& matrices,
                          const std::vector<bool>& hasMatrix) noexcept
{
    int total = 0;
    int nonUnit = 0;
    for (std::size_t i = 0; i < matrices.size(); ++i) {
        if (i >= hasMatrix.size() || !hasMatrix[i]) {
            continue;
        }
        ++total;
        const auto& m = matrices[i];
        const float lx = Length3(m[0], m[1], m[2]);
        const float ly = Length3(m[3], m[4], m[5]);
        const float lz = Length3(m[6], m[7], m[8]);
        const float avg = (lx + ly + lz) / 3.0f;
        if (avg < 0.85f || avg > 1.15f) {
            ++nonUnit;
        }
    }
    return total > 0 && nonUnit * 2 >= total;
}

} // namespace

Vector3 TransformPoint(const std::array<float, 12>& m, const Vector3& p) noexcept
{
    return {
        m[0] * p.x + m[3] * p.y + m[6] * p.z + m[9],
        m[1] * p.x + m[4] * p.y + m[7] * p.z + m[10],
        m[2] * p.x + m[5] * p.y + m[8] * p.z + m[11],
    };
}

Vector3 TransformPointRigid(const std::array<float, 12>& m, const Vector3& p) noexcept
{
    float xx = m[0], xy = m[1], xz = m[2];
    float yx = m[3], yy = m[4], yz = m[5];
    float zx = m[6], zy = m[7], zz = m[8];
    Normalize3(xx, xy, xz);
    Normalize3(yx, yy, yz);
    Normalize3(zx, zy, zz);
    return {
        xx * p.x + yx * p.y + zx * p.z + m[9],
        xy * p.x + yy * p.y + zy * p.z + m[10],
        xz * p.x + yz * p.y + zz * p.z + m[11],
    };
}

std::vector<PreparedPositions> ApplyMatrixPolicy(
    const std::vector<std::vector<Vector3>>& sourcePositions,
    const std::vector<std::array<float, 12>>& matrices,
    const std::vector<bool>& hasMatrix,
    MatrixPolicy policy)
{
    std::vector<PreparedPositions> out(sourcePositions.size());
    for (std::size_t i = 0; i < sourcePositions.size(); ++i) {
        const auto& src = sourcePositions[i];
        out[i].positions.resize(src.size());
        const bool apply = (i < hasMatrix.size() && hasMatrix[i] && policy != MatrixPolicy::Ignore);
        const std::array<float, 12>& m =
            (i < matrices.size()) ? matrices[i] : std::array<float, 12>{};
        for (std::size_t v = 0; v < src.size(); ++v) {
            if (!apply) {
                out[i].positions[v] = src[v];
            } else if (policy == MatrixPolicy::BakeRigid) {
                out[i].positions[v] = TransformPointRigid(m, src[v]);
            } else {
                out[i].positions[v] = TransformPoint(m, src[v]);
            }
        }
    }
    return out;
}

void ApplyAxisPolicy(std::vector<PreparedPositions>& meshes, AxisPolicy policy)
{
    if (policy != AxisPolicy::ConvertZUpToYUp) {
        return;
    }
    for (PreparedPositions& mesh : meshes) {
        for (Vector3& p : mesh.positions) {
            const float y = p.y;
            p.y = p.z;
            p.z = -y;
        }
    }
}

float ScorePreparedLayout(const std::vector<PreparedPositions>& meshes) noexcept
{
    if (meshes.empty()) {
        return -1e30f;
    }
    Aabb global;
    for (const PreparedPositions& mesh : meshes) {
        const Aabb box = BoundsOf(mesh.positions);
        if (box.valid) {
            Expand(global, box.bmin);
            Expand(global, box.bmax);
        }
    }
    if (!global.valid) {
        return -1e30f;
    }
    const float thickness = MeanAbsoluteThickness(meshes);
    const float globalMax = std::max(MaxExtent(global), 1e-6f);
    // Absolute thickness dominates: paper walls from a bad bake score near zero.
    return thickness * 100.0f + std::log(globalMax + 1.0f);
}

MatrixPolicy SelectBestMatrixPolicy(
    const std::vector<std::vector<Vector3>>& sourcePositions,
    const std::vector<std::array<float, 12>>& matrices,
    const std::vector<bool>& hasMatrix)
{
    // Fast path: non-unit pivot matrices almost always mean scene-space verts.
    if (MajorityNonUnitScale(matrices, hasMatrix)) {
        return MatrixPolicy::Ignore;
    }

    // Otherwise compare ignore vs full bake; require bake to win by a clear
    // margin so borderline files keep readable architecture.
    const auto ignore = ApplyMatrixPolicy(sourcePositions, matrices, hasMatrix, MatrixPolicy::Ignore);
    const auto baked = ApplyMatrixPolicy(sourcePositions, matrices, hasMatrix, MatrixPolicy::BakeFull);
    const float sIgnore = ScorePreparedLayout(ignore);
    const float sBake = ScorePreparedLayout(baked);
    if (sBake > sIgnore * 1.25f) {
        return MatrixPolicy::BakeFull;
    }
    return MatrixPolicy::Ignore;
}

AxisPolicy SelectBestAxisPolicy(const std::vector<PreparedPositions>& meshes)
{
    int yUpVotes = 0;
    int zUpVotes = 0;

    for (const PreparedPositions& mesh : meshes) {
        const Aabb box = BoundsOf(mesh.positions);
        if (!box.valid) {
            continue;
        }
        const float ex = box.bmax.x - box.bmin.x;
        const float ey = box.bmax.y - box.bmin.y;
        const float ez = box.bmax.z - box.bmin.z;
        const float maxE = std::max({ex, ey, ez, 1e-6f});
        const float thin = 0.15f * maxE;
        const float wide = 0.4f * maxE;

        if (ey < thin && ex > wide && ez > wide) {
            ++yUpVotes; // floor/ceiling in XZ
        }
        if (ez < thin && ex > wide && ey > wide) {
            if (ey > 3.0f * ez) {
                ++yUpVotes; // Y-up wall
            } else {
                ++zUpVotes; // Z-up floor
            }
        }
        if (ex < thin && ey > wide && ez > wide) {
            if (ey >= ez) {
                ++yUpVotes;
            } else {
                ++zUpVotes;
            }
        }
    }

    return (yUpVotes > zUpVotes) ? AxisPolicy::KeepYUp : AxisPolicy::ConvertZUpToYUp;
}

} // namespace Concord::Asset::ThreeDs
