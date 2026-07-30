#ifndef CONCORD_UIRENDERER_H
#define CONCORD_UIRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/debug/DebugFontAtlas.h"
#include "engine/render/texture/BgfxTextureCache.h"
#include "engine/ui/UiDrawList.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Concord {

/**
 * Draws a UI::DrawList into a window's screen-space overlay view.
 *
 * Reuses the debug-text program (fs samples an R8 coverage texture and
 * modulates the vertex color's alpha): filled rectangles bind a 1x1 white
 * texture so the coverage is 1 and the vertex color passes straight through,
 * while text binds the glyph atlas. Solids and text each collapse into a single
 * submit built into a transient vertex buffer, and image quads add one submit
 * per distinct texture, so a HUD costs two draw calls plus one per image source
 * regardless of widget count. The order is solids, then images, then text, so a
 * panel backs its image and a caption always reads on top of both.
 *
 * Owns only shared GPU resources (program, sampler, white texture, font atlas,
 * vertex layout); the overlay view id and framebuffer belong to the backend.
 * All methods run on the render thread.
 */
class UiRenderer {
public:
    bool EnsureReady();
    void Shutdown();

    /**
     * Draws @p drawList into @p view (already bound to the window framebuffer).
     *
     * @param textures Resolves a DrawCommand's interned TextureId into a GPU
     *        texture for image quads; images are skipped when it is null.
     */
    void Draw(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
              const UI::DrawList& drawList, BgfxTextureCache* textures = nullptr);

private:
    struct UiVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
    };

    void PushQuad(std::vector<UiVertex>& out, float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1, std::uint32_t abgr,
                  float w, float h);
    void PushTriangle(std::vector<UiVertex>& out,
                      float x0, float y0, float x1, float y1, float x2, float y2,
                      std::uint32_t abgr, float w, float h);
    void PushRoundedRect(std::vector<UiVertex>& out, const UI::DrawCommand& command,
                         float w, float h);
    float MeasureText(const std::string& text, float scale);
    void BuildText(std::vector<UiVertex>& out, const UI::DrawCommand& command,
                   float w, float h);
    void SubmitBatch(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
                     const std::vector<UiVertex>& verts, bgfx::TextureHandle texture,
                     bgfx::ProgramHandle program);

    /** One image texture's quads, kept in first-use order for stable layering. */
    struct ImageBatch {
        std::uint32_t texture = 0;                       ///< Interned TextureId.
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE; ///< Already resolved and valid.
        std::vector<UiVertex> verts;
    };

    bool m_ready = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    /** Samples RGBA instead of the coverage channel; used for image quads. */
    bgfx::ProgramHandle m_imageProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_white = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
    DebugFontAtlas m_atlas;
};

} // namespace Concord

#endif // CONCORD_UIRENDERER_H
