#ifndef CONCORD_THREEDSTRANSFORM_H
#define CONCORD_THREEDSTRANSFORM_H

#include "math/Vector3.h"

#include <array>
#include <vector>

namespace Concord::Asset::ThreeDs {

/**
 * How to interpret a 3DS mesh matrix when lifting vertices into scene space.
 *
 * Exporters disagree: some write local verts + a real local→world matrix, others
 * write verts already in scene units and leave a non-unit pivot matrix that
 * must not be baked (doing so collapses walls into paper sheets).
 */
enum class MatrixPolicy {
    /** Never apply the mesh matrix; treat positions as scene units. */
    Ignore,
    /** Always bake the full 4×3 matrix (local → scene). */
    BakeFull,
    /** Bake orthonormalized rotation + translation only (drop scale). */
    BakeRigid,
};

/** Axis policy after mesh matrices are resolved. */
enum class AxisPolicy {
    /** File is already Y-up (engine convention); leave positions alone. */
    KeepYUp,
    /** Classic 3DS Z-up → engine Y-up: (x,y,z) → (x,z,-y). */
    ConvertZUpToYUp,
};

/** One mesh's positions after matrix policy is applied (axis not yet applied). */
struct PreparedPositions {
    std::vector<Vector3> positions;
};

/**
 * Applies `policy` to every mesh's positions. `matrices[i]` / `hasMatrix[i]`
 * describe mesh i; empty matrix entries are treated as identity.
 */
std::vector<PreparedPositions> ApplyMatrixPolicy(
    const std::vector<std::vector<Vector3>>& sourcePositions,
    const std::vector<std::array<float, 12>>& matrices,
    const std::vector<bool>& hasMatrix,
    MatrixPolicy policy);

/** Applies Z-up→Y-up conversion in place when policy requests it. */
void ApplyAxisPolicy(std::vector<PreparedPositions>& meshes, AxisPolicy policy);

/**
 * Scores a prepared multi-mesh layout. Higher is better: rewards non-degenerate
 * thickness (walls not paper-thin) and a sane overall extent.
 */
float ScorePreparedLayout(const std::vector<PreparedPositions>& meshes) noexcept;

/**
 * Picks the matrix policy that best reconstructs the file by scoring
 * Ignore / BakeFull / BakeRigid on the whole mesh set.
 */
MatrixPolicy SelectBestMatrixPolicy(
    const std::vector<std::vector<Vector3>>& sourcePositions,
    const std::vector<std::array<float, 12>>& matrices,
    const std::vector<bool>& hasMatrix);

/**
 * Votes Y-up vs Z-up from prepared positions (floors vs walls). Returns
 * ConvertZUpToYUp only when Z-up floors clearly dominate.
 */
AxisPolicy SelectBestAxisPolicy(const std::vector<PreparedPositions>& meshes);

/** Bakes full 4×3 matrix into a single point (row-vector convention). */
Vector3 TransformPoint(const std::array<float, 12>& m, const Vector3& p) noexcept;

/** Bakes orthonormalized rotation + translation (unit axes, no scale). */
Vector3 TransformPointRigid(const std::array<float, 12>& m, const Vector3& p) noexcept;

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDSTRANSFORM_H
