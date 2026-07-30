#ifndef CONCORD_FLUIDPROGRAMSLOTS_H
#define CONCORD_FLUIDPROGRAMSLOTS_H

#include <cstdint>

namespace Concord {

/**
 * @brief Program slots in BgfxFluidRenderer::m_programs, in substep order.
 *
 * Shared by the lifecycle code (which creates the compute programs in this
 * order) and the dispatch code (which submits them), so the mapping lives in
 * exactly one place. The draw vertex/fragment programs are not stored in
 * m_programs; their slot values only document the embedded-shader table order.
 */
enum FluidProgramSlot : std::uint32_t {
    kSlotGridClear = 0, kSlotGridCount, kSlotGridScan, kSlotGridScatter,
    kSlotDensity, kSlotForces, kSlotDivergence, kSlotPressure, kSlotApply,
    kSlotIntegrate, kSlotFinalize, kSlotFieldSplat, kSlotFieldSmooth,
    kSlotMcVoxels, kSlotMcTriangles,
    kSlotDrawVertex, kSlotDrawFragment,
};

} // namespace Concord

#endif // CONCORD_FLUIDPROGRAMSLOTS_H
