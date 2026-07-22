#ifndef CONCORD_UICONTEXT_H
#define CONCORD_UICONTEXT_H

#include "Concord/CExport.h"
#include "engine/ui/UiDocument.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiInput.h"
#include "engine/ui/UiStyle.h"
#include "engine/ui/UiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::UI {

/**
 * Immediate-mode UI context.
 *
 * A game builds its HUD/menus each frame by calling widget methods between
 * BeginFrame() and EndFrame(). Every call both hit-tests the pointer Input
 * passed to BeginFrame (so Button() reports whether it was clicked) and appends
 * screen-space paint commands to an internal DrawList. The DrawList is plain
 * data handed to the render thread for drawing (Phase 2 UiRenderer), matching
 * the engine's simulation -> snapshot -> render data flow.
 *
 * There is no hidden global state and no bgfx dependency here, so the UI core
 * stays testable and backend-agnostic. Coordinates are screen pixels, top-left
 * origin, y down.
 */
class CENGINE_API Context {
public:
    /** Overrides the widget theme; takes effect from the next frame. */
    void SetStyle(const Style& style);
    const Style& GetStyle() const noexcept { return m_style; }

    /**
     * Starts a frame: clears the draw list and stores the viewport size and the
     * pointer input used for this frame's interaction.
     */
    void BeginFrame(float width, float height, const Input& input);

    /** Fills a rectangle with the given color. */
    void Panel(const Rect& rect, Color color);

    /** Draws left/top-aligned text with its origin at (x, y). */
    void Label(float x, float y, const std::string& text, Color color);

    /**
     * Draws an interactive button with a centered label; returns true on the
     * frame the pointer is pressed and released inside it.
     *
     * @param id A stable, unique, nonzero identifier among this frame's buttons,
     *           so a press on one and release on another is not misread as a
     *           click. Reusing an id across frames is expected and required for
     *           press/release tracking.
     */
    bool Button(std::uint32_t id, const Rect& rect, const std::string& text);

    /** Ends the frame; GetDrawList() is then ready to submit for rendering. */
    void EndFrame();

    /**
     * Draws a whole authored/loaded document in one call: equivalent to
     * BeginFrame, replaying every widget (Panel/Label/Button) with the current
     * style and input, then EndFrame. Button clicks are recorded and queryable
     * with Clicked() until the next Present. This is the simple entry point for
     * a UI loaded from a .cui file.
     */
    void Present(const UiDocument& document, float width, float height,
                 const Input& input);

    /** True if the button with @p id was clicked during the last Present. */
    bool Clicked(std::uint32_t id) const noexcept;

    /** The paint commands built this frame (valid after EndFrame / Present). */
    const DrawList& GetDrawList() const noexcept { return m_drawList; }

private:
    void EmitText(const Rect& rect, const std::string& text, Color color,
                  Align hAlign, Align vAlign, float fontScale);

    Style m_style;
    DrawList m_drawList;
    Input m_input;
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::uint32_t m_activePressId = 0;      ///< Button id captured on press (0 = none).
    std::vector<std::uint32_t> m_clicked;   ///< Ids clicked during the last Present.
};

} // namespace Concord::UI

#endif // CONCORD_UICONTEXT_H
