#ifndef CONCORD_WATERCASCADE_H
#define CONCORD_WATERCASCADE_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderWaterSurface.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Viewer-centred cascade of wave displacement textures.
 *
 * Evaluating a few waves per vertex ties wave detail to mesh density: the near
 * surface comes out faceted and the far surface gets several crests per pixel,
 * which reads as a shivering, jittering field. Crest's approach (published in the
 * SIGGRAPH 2017 Advances in Real-Time Rendering course) turns both into resolution
 * problems, and that is what this class implements:
 *
 * - Every level has the same texel count but covers twice the world extent of the
 *   level below, so texel density falls off with distance like screen density.
 * - A wave is baked only into the level whose texels can represent it (at least
 *   two texels per wavelength), crossfading into the next. Nothing is undersampled,
 *   so there is no aliasing left to filter away.
 * - Level centres follow the viewer but snap to their own texel grid; without the
 *   snap the whole field slides sub-texel every frame and the surface looks like
 *   it is shivering in place.
 *
 * All levels live side by side in one atlas texture so the vertex stage needs a
 * single sampler: RGB holds xyz displacement, alpha the surface Jacobian, which
 * gives the fragment stage both normals and foam from the same data.
 *
 * Owned by the render thread; Shutdown must run before bgfx::shutdown.
 */
class WaterCascade {
public:
    /**
     * Levels in the cascade. Seven at a 2x ratio spans a 64x range: sub-metre
     * detail underfoot out to a horizon-scale swell. The mesh only covers half of
     * each level's extent (see WaterClipmapMesh), so the outermost vertices always
     * sample well inside their level rather than clamping at its border — a
     * clamped lookup gets dragged along with the viewer, which reads as the whole
     * sea sliding when the camera moves.
     */
    static constexpr std::uint32_t kLevels = 7;

    /** Texels per side of one level. */
    static constexpr std::uint32_t kResolution = 256;

    /** World extent of level 0; every level above doubles it. */
    static constexpr float kBaseExtent = 24.0f;

    /** Number of spectrum waves the bake sums; see fs_water_bake. */
    static constexpr std::uint32_t kSpectrumWaves = 48;

    /**
     * World-space grid every level's centre and every clipmap tile snaps to.
     *
     * One shared grid, not one per level. Snapping each level to its own texel
     * size looks equivalent but is not: the levels then sit at slightly different
     * offsets, and since each level's central hole is cut on the assumption that
     * the finer level lands exactly there, any disagreement leaves a real gap in
     * the mesh. The gap widens with every level (up to a tile's worth at the
     * outermost ring) and its width changes as the viewer moves, which is what
     * turns it into shifting slivers of background between the rings.
     *
     * Twice the coarsest level's texel makes this an exact multiple of every
     * level's texel and of every ring's vertex parity, so all the rings nest
     * perfectly and none of them crawl.
     */
    static constexpr float kSharedSnap =
        2.0f * (kBaseExtent * static_cast<float>(1u << (kLevels - 1))
                / static_cast<float>(kResolution));

    /** Snaps a viewer coordinate to the shared centre grid. */
    static float SnapCentre(float value) noexcept;

    bool EnsureReady();
    void Shutdown();
    bool Ready() const noexcept { return m_ready; }

    /**
     * Rebakes the whole cascade for @p surface, centred on the viewer.
     *
     * @param view Bake view id; must be ordered before the water draw so the
     *             atlas is complete when the surface samples it.
     */
    void Update(RenderViewHandle view, const RenderWaterSurface& surface,
                float viewerX, float viewerZ);

    /** The cascade atlas: kLevels columns of kResolution texels. */
    bgfx::TextureHandle Atlas() const noexcept { return m_atlas; }

    /**
     * Per-level world-to-UV mapping as (centreX, centreZ, extent, texelWorldSize),
     * kLevels entries, for the water vertex and fragment stages.
     */
    const float* LevelParams() const noexcept { return &m_levelParams[0][0]; }

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;

    bgfx::TextureHandle m_atlas = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_framebuffer = BGFX_INVALID_HANDLE;
    float m_levelParams[kLevels][4]{};

    bgfx::ProgramHandle m_bakeProgram = BGFX_INVALID_HANDLE;
    /** (centreX, centreZ, extent, texelWorldSize) of the level being baked. */
    bgfx::UniformHandle m_uLevel = BGFX_INVALID_HANDLE;
    /** (time, choppiness, topLevel, atlasColumnCount). */
    bgfx::UniformHandle m_uSpectrum = BGFX_INVALID_HANDLE;
    /** Legacy Gerstner payload: (amplitude, wavelength, speed, directionDegrees). */
    bgfx::UniformHandle m_uGerstner = BGFX_INVALID_HANDLE;
    /** Wind-spectrum payload: (windSpeed, windDirection, spreadDegrees, amplitudeScale). */
    bgfx::UniformHandle m_uWind = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_quadVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_WATERCASCADE_H

