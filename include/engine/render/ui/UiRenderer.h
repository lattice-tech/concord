#ifndef CONCORD_UIRENDERER_H
#define CONCORD_UIRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/debug/DebugFontAtlas.h"
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
 * while text binds the glyph atlas. The whole draw list collapses into at most
 * two submits - all solids, then all text - built into transient vertex buffers,
 * so a HUD costs two draw calls regardless of widget count. Solids are drawn
 * before text, so labels always sit on top of their panels.
 *
 * Owns only shared GPU resources (program, sampler, white texture, font atlas,
 * vertex layout); the overlay view id and framebuffer belong to the backend.
 * All methods run on the render thread.
 */
class UiRenderer {
public:
    bool EnsureReady();
    void Shutdown();

    /** Draws @p drawList into @p view (already bound to the window framebuffer). */
    void Draw(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
              const UI::DrawList& drawList);

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
    float MeasureText(const std::string& text, float scale);
    void BuildText(std::vector<UiVertex>& out, const UI::DrawCommand& command,
                   float w, float h);
    void SubmitBatch(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
                     const std::vector<UiVertex>& verts, bgfx::TextureHandle texture);

    bool m_ready = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_white = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
    DebugFontAtlas m_atlas;
};

} // namespace Concord

#endif // CONCORD_UIRENDERER_H
