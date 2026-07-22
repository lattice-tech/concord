#ifndef CONCORD_BGFXMESHPIPELINE_H
#define CONCORD_BGFXMESHPIPELINE_H

#include <bgfx/bgfx.h>

namespace Concord {

/**
 * Owns the embedded mesh shading program (`vs_mesh` + `fs_mesh`), lazily built
 * from the per-API compiled bytecodes the build script bakes into a generated
 * header, and torn down through `Shutdown`. The render backend owns one; this
 * component is intentionally narrow so the shader-loading concern (parse the
 * embedded blob, ask bgfx for the right per-API variant, link a program) is
 * isolated from mesh storage and uniform binding (AGENTS.md §5).
 *
 * Workflow is a one-shot create: `EnsureReady` is called once when the mesh
 * pass first needs to draw; `Ready` / `Program` let the caller query what
 * came up; `Shutdown` releases the program. All methods run on the render
 * thread (bgfx is single-threaded by construction).
 */
class BgfxMeshPipeline {
public:
    /**
     * Lazily creates the mesh program for the active bgfx renderer.
     * Idempotent: a failed attempt is recorded and not retried until
     * `Shutdown`/`Reset`; consecutive calls return the cached result.
     * @return true once the program is live; false on any creation failure.
     */
    bool EnsureReady();

    /**
     * Lazily creates the skinned mesh program (`vs_mesh_skinned` + `fs_mesh`),
     * used for GPU linear-blend-skinned draws. Same idempotent semantics as
     * EnsureReady; brought up only the first time a skinned mesh is drawn.
     * @return true once the skinned program is live.
     */
    bool EnsureSkinnedReady();

    /** Lazily creates the procedural particle billboard program. */
    bool EnsureParticleReady();

    /** Releases all programs; safe when never ready or after a previous Shutdown. */
    void Shutdown();

    /** Forget the attempted/ready state so the next `EnsureReady` can try again after a Reset cycle. */
    void Reset();

    /** True once the program is live and usable for `submit`. */
    bool Ready() const noexcept { return m_programReady; }

    /** True once the skinned program is live. */
    bool SkinnedReady() const noexcept { return m_skinnedReady; }

    /** True once the particle billboard program is live. */
    bool ParticleReady() const noexcept { return m_particleReady; }

    /** The bgfx program handle; invalid before a successful `EnsureReady`. */
    bgfx::ProgramHandle Program() const noexcept { return m_program; }

    /** The skinned program handle; invalid before a successful `EnsureSkinnedReady`. */
    bgfx::ProgramHandle SkinnedProgram() const noexcept { return m_skinnedProgram; }

    /** The particle billboard program handle. */
    bgfx::ProgramHandle ParticleProgram() const noexcept { return m_particleProgram; }

private:
    bool m_programReady = false;
    bool m_programAttempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;

    bool m_skinnedReady = false;
    bool m_skinnedAttempted = false;
    bgfx::ProgramHandle m_skinnedProgram = BGFX_INVALID_HANDLE;

    bool m_particleReady = false;
    bool m_particleAttempted = false;
    bgfx::ProgramHandle m_particleProgram = BGFX_INVALID_HANDLE;
};

} // namespace Concord

#endif // CONCORD_BGFXMESHPIPELINE_H
