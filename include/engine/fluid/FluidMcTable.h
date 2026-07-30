#ifndef CONCORD_FLUIDMCTABLE_H
#define CONCORD_FLUIDMCTABLE_H

#include <cstdint>

namespace Concord::Fluid {

/**
 * Canonical Lorensen & Cline Marching Cubes triangulation table: for each of
 * the 256 corner-inside cases, up to 5 triangles as triples of cube-edge
 * indices, padded with -1. Corner i lies at (i&1, (i>>1)&1, (i>>2)&1) and a
 * case bit marks the corner as inside (field > iso). Edges:
 *   0: 0-1   1: 1-2   2: 2-3   3: 3-0
 *   4: 4-5   5: 5-6   6: 6-7   7: 7-4
 *   8: 0-4   9: 1-5  10: 2-6  11: 3-7
 *
 * Kept on the CPU so a regression test can validate its structure (cut-edge
 * membership and complement symmetry) and the GPU simply uploads it once
 * into a read-only buffer for the sparse-MC compute passes.
 */
extern const int kFluidMcTriTable[256 * 16];

/** Per-case triangle count derived from kFluidMcTriTable (0..5). */
int FluidMcTriangleCount(int caseIndex) noexcept;

/**
 * Structural self-check of the table: every entry is a valid, actually-cut
 * edge of its case; triangle counts are multiples of 3 within range; cases 0
 * and 255 are empty; and every emitted triangle is non-degenerate.
 * Complement symmetry is intentionally NOT required: the canonical table
 * triangulates the ambiguous cases asymmetrically to avoid interior holes.
 * @return true when the table is structurally sound.
 */
bool ValidateFluidMcTable() noexcept;

} // namespace Concord::Fluid

#endif // CONCORD_FLUIDMCTABLE_H
