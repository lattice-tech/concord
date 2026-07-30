#ifndef CONCORD_WATERBODY_H
#define CONCORD_WATERBODY_H

#include "Concord/CExport.h"
#include "engine/object/Node.h"
#include "engine/render/frame/RenderWaterSurface.h"
#include "engine/water/WaterSurfaceDesc.h"
#include "engine/water/WaterSurfaceState.h"
#include "engine/water/WaterWave.h"
#include "math/Vector3.h"

#include <vector>

namespace Concord::Object {

/**
 * A body of water: a first-class scene node the water pass draws as a displaced,
 * refracting, reflecting surface.
 *
 * Created through `Scene::Spawn<WaterBody>(desc)`, usually from a preset:
 * `Water::Presets::StillLake()`, `WavyLake()`, `StaticRiver()` or
 * `FlowingRiver()`. The surface is a rectangle in the node's local XZ plane at
 * local y = 0, so the inherited Node transform places and orients it and moving
 * an ancestor carries the water with it.
 *
 * The node owns the animation clock and advances it by `deltaTime` each frame
 * (frozen while the motion is Still), then hands the render thread the resolved
 * wave octaves plus that clock. Gameplay evaluates the very same waves on the
 * CPU through SurfaceHeight/SurfacePoint, so buoyancy and splash spawning line
 * up with what is drawn instead of drifting from it.
 */
class CENGINE_API WaterBody : public Node {
public:
    explicit WaterBody(Water::WaterSurfaceDesc desc = {});

    /** The description this body was built from, including any runtime retuning. */
    const Water::WaterSurfaceDesc& Desc() const noexcept { return m_desc; }

    Water::WaterKind Kind() const noexcept { return m_desc.kind; }
    Water::WaterMotion Motion() const noexcept { return m_desc.motion; }

    /** Resolved wave octaves currently driving the surface. */
    const Water::WaveSet& Waves() const noexcept { return m_waves; }

    /** Seconds of animation accumulated so far (0 for a still surface). */
    float Time() const noexcept { return m_state.time; }

    /**
     * World-space height of the surface under a world position.
     *
     * Only defined over the surface rectangle; outside it the result is the
     * extrapolated wave field, which is what a caller checking "am I above the
     * water" wants anyway. Returns the plane height for a still surface.
     */
    float SurfaceHeight(float worldX, float worldZ) const;

    /** World-space displaced point and normal under a world position. */
    Water::SurfacePoint SurfacePoint(float worldX, float worldZ) const;

    /** True when a world position is inside the surface rectangle (XZ only). */
    bool ContainsPoint(float worldX, float worldZ) const;

    /** True when a world position lies below the displaced surface. */
    bool IsSubmerged(const Vector3& worldPosition) const;

    /** Replaces the wave shape; a count of 0 flattens the surface. */
    void SetWaves(std::uint32_t count, float amplitude, float wavelength,
                  float steepness, float speed, float directionDegrees);

    /** Sets the current in world units per second and its heading in degrees. */
    void SetFlow(float speed, float directionDegrees);

    /** Switches between a mirror-still and an animated surface. */
    void SetMotion(Water::WaterMotion motion) noexcept;

    /** Sets the depth gradient endpoints, packed 0xRRGGBBAA (sRGB). */
    void SetColors(std::uint32_t shallowColor, std::uint32_t deepColor) noexcept;

    /** Sets the authored depth in world units (clamped to > 0). */
    void SetDepth(float depth) noexcept;

    /** Sets the foam band width in world units and its opacity in [0, 1]. */
    void SetFoam(float width, float intensity) noexcept;

    /**
     * Sets the wind-driven sea state. When the surface is Dynamic and
     * `wind.windSpeed > 0`, the bake shader expands this spectrum on the GPU;
     * the CPU-side WaveSet is rebuilt from it so buoyancy tracks the same sea.
     */
    void SetWind(const Water::WindState& wind) noexcept;

    /** Sets the advanced optical terms the high-end fragment shader consumes. */
    void SetOptics(const Water::WaterOptics& optics) noexcept;

    /** True when the wind-driven spectrum will drive the bake this frame. */
    bool UseWindSpectrum() const noexcept
    {
        return m_state.waveSource == Water::WaterWaveSource::WindSpectrum;
    }

    /**
     * The flat, resolved form of this surface for the current frame.
     *
     * This is exactly what the Scene hands the render thread each frame; it is
     * public so tools and tests can inspect the resolved values without driving
     * a whole frame through a window.
     */
    RenderWaterSurface ResolveSurface() const;

private:
    void Advance(float deltaTime);
    void CollectWaterSurfaces(std::vector<RenderWaterSurface>& out) const override;

    /** Rebuilds `m_waves` from the descriptor, honouring the motion mode. */
    void RebuildWaves();

    /** Resolves runtime-only state from the authored description. */
    void ResolveRuntimeState() noexcept;

    /** Which wave source should drive the surface this frame. */
    Water::WaterWaveSource ResolveWaveSource() const noexcept;

    /** Maps a world XZ position into the surface's local XZ plane. */
    void ToLocalXZ(float worldX, float worldZ, float& outX, float& outZ) const;

    Water::WaterSurfaceDesc m_desc;
    Water::WaveSet m_waves{};
    Water::WaterSurfaceState m_state{};
};

} // namespace Concord::Object

#endif // CONCORD_WATERBODY_H
