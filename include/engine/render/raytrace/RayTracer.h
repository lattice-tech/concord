#ifndef CONCORD_RAYTRACER_H
#define CONCORD_RAYTRACER_H

#include "engine/render/backend/IRenderBackend.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Analytic-sphere ray tracer for objects flagged SetRayTraced.
 *
 * bgfx has no DXR/VK_KHR_ray_tracing path, so primary hits use analytic
 * spheres (AABB-derived). Reflections bounce between those spheres, then sample
 * a real-time scene cubemap captured at the dominant sphere. Primary
 * visibility uses four rotated subpixel rays. View order is scene → rtCompute
 * → rtResolve; resolve coverage-blends and depth-tests into the HDR scene
 * target before bloom/present.
 *
 * This isolated experimental utility is no longer wired into the primary mesh
 * renderer: screen-space analytic sphere composition looked like a flat circle.
 * The active path keeps real mesh geometry and samples ReflectionCapture instead.
 */
class RayTracer {
public:
    /** Upper bound on ray-traced spheres per frame (matches the shader array). */
    static constexpr std::uint32_t kMaxSpheres = 8;

    /**
     * One ray-traced object, reduced to an analytic sphere. Material fields
     * drive the multi-bounce Fresnel, diffuse and rough-reflection response.
     */
    struct Sphere {
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = 1.0f;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float reflectivity = 0.8f;
        float roughness = 0.05f;
        float metallic = 1.0f;
    };

    RayTracer() = default;
    ~RayTracer();

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    /** True when the active renderer advertises compute-shader support. */
    bool Supported() const;

    /**
     * Lazily creates the compute program, the fullscreen resolve program, their
     * uniforms/sampler and the fullscreen-triangle vertex buffer. Idempotent; a
     * failed attempt is not retried until Shutdown. Returns false (and stays a
     * no-op) when compute is unsupported or a resource fails to come up.
     */
    bool EnsureReady();

    /** Releases every resource; safe when never readied or already shut down. */
    void Shutdown();

    /** Whether EnsureReady has succeeded and Trace can run. */
    bool Ready() const noexcept { return m_ready; }

    /**
     * Creates the rgba32f output image (compute-writable, point-sampled) sized
     * `width` x `height`. Returns false and leaves `tex` invalid on failure.
     */
    bool CreateImage(std::uint32_t width, std::uint32_t height, bgfx::TextureHandle& tex) const;

    /** Destroys an image previously handed out by CreateImage. */
    void DestroyImage(bgfx::TextureHandle& tex) const;

    /**
     * Traces `spheres` into `target` on `computeView`, then submits the
     * fullscreen resolve into `resolveView` (same framebuffer as the scene,
     * higher view id).
     *
     * @param invViewProj Row-major inverse camera view-projection (primary rays).
     * @param viewProj     Row-major camera view-projection (hit depth).
     * @param camPos       Camera world position (ray origin).
     * @param lightDir     Direction the key light travels (world space).
     * @param skyAmbient   Ambient fill strength added to sphere shading.
     * @param environment  Current-frame linear HDR cubemap around the receiver.
     * A no-op when not Ready or `count` is zero.
     */
    void Trace(RenderViewHandle computeView, RenderViewHandle resolveView,
               bgfx::TextureHandle target, std::uint32_t width, std::uint32_t height,
               const float invViewProj[16], const float viewProj[16],
               const float camPos[3], const float lightDir[3], const float sunColor[3],
               const float skyColor[3], float skyAmbient, bgfx::TextureHandle environment,
               const Sphere* spheres, std::uint32_t count);

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_computeProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_resolveProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCamera = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uOptions = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uLight = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSun = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSky = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uInvViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSpheres = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uResolveParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sRtColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sRtEnvironment = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_RAYTRACER_H
