#ifndef CONCORD_UIDOCUMENT_H
#define CONCORD_UIDOCUMENT_H

#include "engine/ui/UiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::UI {

/** The kind of an authored widget in a UI document. */
enum class WidgetKind : std::uint8_t {
    Panel,  ///< Filled rectangle background.
    Label,  ///< Static text.
    Button, ///< Interactive text button (identified by id).
};

/**
 * One authored, serializable UI element.
 *
 * This is the retained, on-disk unit (distinct from the per-frame render-side
 * DrawCommand): a document is a flat list of these, saved to / loaded from a
 * .cui file and replayed each frame through Context::Present. A flat list keeps
 * both serialization and per-frame replay a single linear pass - cache-friendly
 * and allocation-free once reserved.
 */
struct Widget {
    WidgetKind kind = WidgetKind::Panel;
    std::uint32_t id = 0;          ///< Button interaction id; 0 = non-interactive.
    Rect rect;
    Color color = Rgba(255, 255, 255, 255); ///< Panel fill, or Label text color.
    std::string text;              ///< Label / Button caption.
    Align hAlign = Align::Center;
    Align vAlign = Align::Center;
    float fontScale = 1.0f;
};

/**
 * A flat, serializable collection of widgets making up one screen / panel of
 * UI. Authored in code or loaded from a .cui file (see UiDocumentIO), then
 * drawn and interacted with via Context::Present.
 */
struct UiDocument {
    std::vector<Widget> widgets;

    void Clear() noexcept { widgets.clear(); }

    /** Appends a filled background panel. */
    Widget& AddPanel(const Rect& rect, Color color)
    {
        return widgets.emplace_back(Widget{WidgetKind::Panel, 0, rect, color, {},
                                           Align::Start, Align::Center, 1.0f});
    }

    /** Appends a static text label anchored at the rect (Start/Start aligns to top-left). */
    Widget& AddLabel(const Rect& rect, const std::string& text, Color color)
    {
        return widgets.emplace_back(Widget{WidgetKind::Label, 0, rect, color, text,
                                           Align::Start, Align::Start, 1.0f});
    }

    /** Appends an interactive button with a centered caption and the given id. */
    Widget& AddButton(std::uint32_t id, const Rect& rect, const std::string& text)
    {
        return widgets.emplace_back(Widget{WidgetKind::Button, id, rect,
                                           Rgba(255, 255, 255, 255), text,
                                           Align::Center, Align::Center, 1.0f});
    }
};

} // namespace Concord::UI

#endif // CONCORD_UIDOCUMENT_H
