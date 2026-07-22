#ifndef CONCORD_BGFXGPUPARTICLERENDERER_H
#define CONCORD_BGFXGPUPARTICLERENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderParticleEmitter.h"

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Concord {

/**
 * @brief Owns persistent GPU particle pools and their compute/draw programs.
 *
 * One state buffer is retained for each `(RenderViewHandle, emitterKey)` pair.
 * Simulation runs once before the owning scene view and the same buffer is then
 * consumed as instance data by the billboard draw. All methods run on the
 * process render thread.
 */
class BgfxGpuParticleRenderer {
public:
    ~BgfxGpuParticleRenderer();

    BgfxGpuParticleRenderer() = default;
    BgfxGpuParticleRenderer(const BgfxGpuParticleRenderer&) = delete;
    BgfxGpuParticleRenderer& operator=(const BgfxGpuParticleRenderer&) = delete;

    /** Returns whether the active bgfx device supports compute and instancing. */
    bool Supported() const;

    /** Creates the shared compute/draw programs, uniforms, and quad geometry. */
    bool EnsureReady();

    /**
     * Advances every emitter packet for one window on `computeView`.
     * Cumulative packet fields recover simulation work when snapshots skip.
     */
    void Simulate(RenderViewHandle ownerView, RenderViewHandle computeView,
                  const std::vector<RenderParticleEmitter>& emitters);

    /** Draws the current state of every supported emitter into `drawView`. */
    void Draw(RenderViewHandle ownerView, RenderViewHandle drawView,
              const std::vector<RenderParticleEmitter>& emitters,
              const float clipPlane[4] = nullptr);

    /** Releases pools associated with a destroyed window view. */
    void DestroyView(RenderViewHandle ownerView);

    /** Retires emitter pools that have not appeared in recent snapshots. */
    void EndFrame();

    /** Releases all shared and per-emitter GPU resources. */
    void Shutdown();

    /** True after shared GPU resources have been created successfully. */
    bool Ready() const noexcept { return m_ready; }

private:
    static constexpr std::uint32_t kThreadGroupSize = 64;

    struct EmitterKey {
        RenderViewHandle ownerView = kInvalidRenderView;
        std::uintptr_t emitterKey = 0;

        bool operator==(const EmitterKey& other) const noexcept
        {
            return ownerView == other.ownerView && emitterKey == other.emitterKey;
        }
    };

    struct EmitterKeyHash {
        std::size_t operator()(const EmitterKey& key) const noexcept;
    };

    struct EmitterState {
        bgfx::DynamicVertexBufferHandle particles = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0;
        std::uint32_t resetGeneration = 0;
        std::uint64_t spawnSequence = 0;
        double simulationTime = 0.0;
        std::uint64_t lastSeenFrame = 0;
        bool initialized = false;
    };

    bool CreateSharedResources();
    bool EnsureEmitterState(EmitterState& state, std::uint32_t capacity);
    void DestroyEmitterState(EmitterState& state);
    void DispatchStep(RenderViewHandle computeView, EmitterState& state,
                      const RenderParticleEmitter& emitter, float deltaTime,
                      std::uint64_t spawnBegin, std::uint32_t spawnCount,
                      bool reset, float prewarmSeconds);
    void BindSimulationUniforms(const RenderParticleEmitter& emitter,
                                std::uint32_t capacity, float deltaTime,
                                std::uint32_t spawnStart, std::uint32_t spawnCount,
                                std::uint64_t spawnBegin, bool reset,
                                float prewarmSeconds);
    void BindDrawUniforms(const RenderParticleEmitter& emitter,
                          const float clipPlane[4]);

    bool m_ready = false;
    bool m_attempted = false;
    std::uint64_t m_frameNumber = 1;
    bgfx::ProgramHandle m_computeProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_drawProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSimulation = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uForceFields = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uEmitterWorld = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uEmitterRotation = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uEmitterInverseRotation = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uClipPlane = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uVisual = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uDraw = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uColorStart = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uColorMid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uColorEnd = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_quadVertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_quadIndexBuffer = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_particleLayout;
    std::unordered_map<EmitterKey, EmitterState, EmitterKeyHash> m_emitters;
};

} // namespace Concord

#endif // CONCORD_BGFXGPUPARTICLERENDERER_H
