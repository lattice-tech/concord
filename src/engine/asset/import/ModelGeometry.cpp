#include "engine/asset/import/ModelGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Concord::Asset {

namespace {

void ExpandBounds(Vector3& bmin, Vector3& bmax, const Vector3& p, bool& init) noexcept
{
    if (!init) {
        bmin = bmax = p;
        init = true;
        return;
    }
    bmin.x = std::min(bmin.x, p.x);
    bmin.y = std::min(bmin.y, p.y);
    bmin.z = std::min(bmin.z, p.z);
    bmax.x = std::max(bmax.x, p.x);
    bmax.y = std::max(bmax.y, p.y);
    bmax.z = std::max(bmax.z, p.z);
}

void Renormalize(Vector3& n) noexcept
{
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 1e-12f) {
        n.x /= len;
        n.y /= len;
        n.z /= len;
    } else {
        n = Vector3{0.0f, 1.0f, 0.0f};
    }
}

void DropEmptySubMeshes(ImportedModel& model)
{
    model.meshes.erase(
        std::remove_if(model.meshes.begin(), model.meshes.end(),
                       [](const ImportedSubMesh& sub) {
                           return sub.geometry.positions.empty()
                               || (sub.geometry.indices.empty()
                                   && sub.geometry.indices32.empty());
                       }),
        model.meshes.end());
}

} // namespace

void RecomputeBounds(ImportedModel& model) noexcept
{
    bool init = false;
    Vector3 bmin{};
    Vector3 bmax{};
    for (const ImportedSubMesh& sub : model.meshes) {
        for (const Vector3& p : sub.geometry.positions) {
            ExpandBounds(bmin, bmax, p, init);
        }
    }
    if (init) {
        model.boundsMin = bmin;
        model.boundsMax = bmax;
    } else {
        model.boundsMin = model.boundsMax = Vector3{};
    }
}

void NormalizeToModelSpace(ImportedModel& model, float fitExtent) noexcept
{
    RecomputeBounds(model);
    if (!model.HasGeometry()) {
        return;
    }

    const float dx = model.boundsMax.x - model.boundsMin.x;
    const float dy = model.boundsMax.y - model.boundsMin.y;
    const float dz = model.boundsMax.z - model.boundsMin.z;
    const float maxExtent = std::max({dx, dy, dz});
    if (maxExtent <= 1e-8f) {
        return;
    }

    // XZ only: keep the footprint under the origin. Do NOT pre-center Y —
    // ground-align below is the sole vertical placement, so floor height stays
    // predictable and multi-storey structure is not shifted twice.
    const float centerX = (model.boundsMin.x + model.boundsMax.x) * 0.5f;
    const float centerZ = (model.boundsMin.z + model.boundsMax.z) * 0.5f;
    const float target = fitExtent > 1e-6f ? fitExtent : 2.0f;
    const float scale = target / maxExtent;

    float minY = std::numeric_limits<float>::max();
    for (ImportedSubMesh& sub : model.meshes) {
        for (Vector3& p : sub.geometry.positions) {
            p.x = (p.x - centerX) * scale;
            p.y = p.y * scale;
            p.z = (p.z - centerZ) * scale;
            minY = std::min(minY, p.y);
        }
        for (Vector3& n : sub.geometry.normals) {
            Renormalize(n);
        }
    }

    // Ground-align: lowest vertex sits on the local y = 0 plane.
    if (minY != std::numeric_limits<float>::max() && std::fabs(minY) > 1e-8f) {
        for (ImportedSubMesh& sub : model.meshes) {
            for (Vector3& p : sub.geometry.positions) {
                p.y -= minY;
            }
        }
    }

    RecomputeBounds(model);
}

void FinalizeModelGeometry(ImportedModel& model, const ModelGeometryOptions& options) noexcept
{
    DropEmptySubMeshes(model);
    if (!model.HasGeometry()) {
        model.boundsMin = model.boundsMax = Vector3{};
        return;
    }
    if (options.normalize) {
        NormalizeToModelSpace(model, options.fitExtent);
    } else {
        RecomputeBounds(model);
    }
}

} // namespace Concord::Asset
