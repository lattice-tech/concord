#ifndef CONCORD_FLUIDWATER_H
#define CONCORD_FLUIDWATER_H

#include "Concord/CExport.h"
#include "engine/fluid/FluidDesc.h"
#include "engine/object/Node.h"
#include "engine/render/frame/RenderFluid.h"

#include <vector>

namespace Concord::Object {

/**
 * @brief A DFSPH fluid body: a tank of water simulated as particles on the
 * GPU and rendered as a reconstructed free surface with true dual-interface
 * refraction.
 *
 * Created through `Scene::Spawn<FluidWater>(desc)`. The tank is an
 * axis-aligned box centred on the node's local origin; the inherited Node
 * transform places and rotates it (keep scale uniform — the solver works in
 * local space and assumes the world mapping is rigid up to uniform scale).
 *
 * The simulation itself runs on the render thread's compute queue (GPU
 * DFSPH: divergence-free velocity solve + constant-density solve every
 * substep, so the water neither compresses nor inflates), while this node
 * owns the authoring data, consumes the per-frame simulation delta, and
 * hands the render thread a flat RenderFluid snapshot. Gameplay interacts
 * through Reset/Pause; per-particle queries are intentionally not exposed.
 */
class CENGINE_API FluidWater : public Node {
public:
    explicit FluidWater(Fluid::FluidTankDesc desc = {});

    /** The normalized description this body was built from. */
    const Fluid::FluidTankDesc& Desc() const noexcept { return m_desc; }

    /** The resolved counts/sizes derived from the description. */
    const Fluid::FluidLayout& Layout() const noexcept { return m_layout; }

    /** Reseeds every particle into the authored fill box on the next frame. */
    void Reset() noexcept { m_resetPending = true; }

    /** Pauses/resumes the simulation; the last surface keeps rendering. */
    void SetPaused(bool paused) noexcept { m_desc.paused = paused; }
    bool Paused() const noexcept { return m_desc.paused; }

    /** Runtime optics tweaks; no respawn required. */
    void SetWaterColor(std::uint32_t rgba) noexcept { m_desc.waterColor = rgba; }
    void SetAbsorption(float absorption) noexcept;
    void SetIor(float ior) noexcept;

    /**
     * The flat, resolved form of this body for the current frame.
     *
     * Public so tools and tests can inspect the snapshot without driving a
     * whole frame through a window. The frame delta accumulated since the
     * previous resolve is consumed (reported once), mirroring the GPU
     * particle contract.
     */
    RenderFluid ResolveFluid() const;

private:
    void Advance(float deltaTime);
    void CollectFluids(std::vector<RenderFluid>& out) const override;

    Fluid::FluidTankDesc m_desc;
    Fluid::FluidLayout m_layout;
    mutable float m_pendingDelta = 0.0f;
    mutable bool m_resetPending = true; ///< Seed on the first rendered frame.
};

} // namespace Concord::Object

#endif // CONCORD_FLUIDWATER_H
