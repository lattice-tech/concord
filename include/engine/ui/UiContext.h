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
 * Observable interaction state produced for one control in the current frame.
 * `activated` is a one-frame edge from pointer release or keyboard activation.
 */
struct WidgetFeedback {
    bool hovered = false;
    bool focused = false;
    bool pressed = false;
    bool activated = false;
    bool disabled = false;
};

/**
 * @brief Builds interaction feedback and draw commands for one runtime UI surface.
 *
 * Call widget methods between BeginFrame() and EndFrame() on the simulation
 * coordinator. One Context owns one independent set of focus and press state.
 * Coordinates are framebuffer pixels with a top-left origin and y pointing down.
 */
class CENGINE_API Context {
public:
    /** Overrides the widget theme; takes effect from the next frame. */
    void SetStyle(const Style& style);
    const Style& GetStyle() const noexcept { return m_style; }

    /**
     * Starts a frame: clears per-frame output, stores input, and applies focus
     * navigation against the previous frame's stable control order.
     */
    void BeginFrame(float width, float height, const Input& input);

    /** Fills a rectangle and captures world-pointer input while it is hovered. */
    void Panel(const Rect& rect, Color color);

    /** Draws a panel with optional border and rounded corners. */
    void Panel(const Rect& rect, Color fill, Color borderColor,
               float borderThickness, float cornerRadius = 0.0f);

    /** Draws left/top-aligned text with its origin at (x, y). */
    void Label(float x, float y, const std::string& text, Color color);

    /**
     * Draws an image stretched to fill @p rect, modulated by @p tint.
     *
     * The path is interned to a TextureId here (a cheap, thread-safe lookup that
     * touches no file and no GPU resource), so the draw list stays free of
     * strings and ownership while crossing to the render thread, which decodes
     * and uploads the image once and shares it with every other user of the same
     * path. An empty path is ignored; a path that fails to load is reported once
     * by the texture cache and drawn as the neutral white fallback.
     *
     * Images do not capture pointer input: overlay an invisible Panel or a
     * Button when the image must be clickable.
     */
    void Image(const Rect& rect, const std::string& texturePath,
               Color tint = Rgba(255, 255, 255, 255));

    /**
     * Draws an interactive button with a centered label; returns true when a
     * captured pointer press is released inside it or keyboard activation is
     * requested while it owns focus.
     *
     * @param id A stable, unique, nonzero identifier among this frame's buttons,
     *           so a press on one and release on another is not misread as a
     *           click. Reusing an id across frames is expected and required for
     *           press/release tracking.
     */
    bool Button(std::uint32_t id, const Rect& rect, const std::string& text);

    /** Draws a button with a complete per-instance visual override. */
    bool Button(std::uint32_t id, const Rect& rect, const std::string& text,
                const ButtonStyle& style, bool enabled = true);

    /**
     * Draws a button and returns all of its interaction feedback for this frame.
     * Disabled buttons remain pointer-opaque but cannot focus, press, or activate.
     * An id of zero is always treated as disabled because zero is the internal
     * no-control sentinel.
     */
    WidgetFeedback ButtonWithFeedback(std::uint32_t id, const Rect& rect,
                                       const std::string& text, bool enabled = true);

    /** Draws a button with per-instance visuals and returns interaction feedback. */
    WidgetFeedback ButtonWithFeedback(std::uint32_t id, const Rect& rect,
                                      const std::string& text,
                                      const ButtonStyle& style,
                                      bool enabled = true);

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

    /** Returns the current frame's feedback for @p id, or an empty state. */
    WidgetFeedback GetFeedback(std::uint32_t id) const noexcept;

    /** Stable id of the keyboard-focused control, or zero when none is focused. */
    std::uint32_t FocusedId() const noexcept { return m_focusedId; }

    /** True when a panel/control owns the pointer for this frame. */
    bool WantsPointer() const noexcept { return m_wantsPointer; }

    /** True when focus or a navigation input makes this context own the keyboard. */
    bool WantsKeyboard() const noexcept { return m_wantsKeyboard; }

    /** True only on a frame whose Input requested cancellation. */
    bool WasCancelled() const noexcept { return m_cancelled; }

    /** The paint commands built this frame (valid after EndFrame / Present). */
    const DrawList& GetDrawList() const noexcept { return m_drawList; }

private:
    struct FeedbackEntry {
        std::uint32_t id = 0;
        WidgetFeedback feedback;
    };

    void EmitSolidRect(const Rect& rect, Color color);
    void EmitStyledRect(const Rect& rect, Color fill, Color borderColor,
                        float borderThickness, float cornerRadius);
    void EmitTexturedRect(const Rect& rect, std::uint32_t texture, Color tint);
    void EmitText(const Rect& rect, const std::string& text, Color color,
                  Align hAlign, Align vAlign, float fontScale);
    void DrawFocusBorder(const Rect& rect, Color color, float thickness);
    void MoveFocus(bool backwards);
    void StoreFeedback(std::uint32_t id, const WidgetFeedback& feedback);
    bool WasSeen(std::uint32_t id) const noexcept;

    Style m_style;
    DrawList m_drawList;
    Input m_input;
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::uint32_t m_activePressId = 0;      ///< Button id captured on press (0 = none).
    std::uint32_t m_focusedId = 0;
    std::vector<std::uint32_t> m_clicked;   ///< Ids clicked during the last Present.
    std::vector<std::uint32_t> m_previousFocusOrder;
    std::vector<std::uint32_t> m_focusOrder;
    std::vector<std::uint32_t> m_seenIds;
    std::vector<FeedbackEntry> m_feedback;
    bool m_activeSeen = false;
    bool m_pendingInitialFocus = false;
    bool m_pendingInitialFocusBackwards = false;
    bool m_wantsPointer = false;
    bool m_wantsKeyboard = false;
    bool m_cancelled = false;
};

} // namespace Concord::UI

#endif // CONCORD_UICONTEXT_H
