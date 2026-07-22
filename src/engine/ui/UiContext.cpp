#include "engine/ui/UiContext.h"

#include <utility>

namespace Concord::UI {

void Context::SetStyle(const Style& style)
{
    m_style = style;
}

void Context::BeginFrame(float width, float height, const Input& input)
{
    m_width = width;
    m_height = height;
    m_input = input;
    m_drawList.Clear();
}

void Context::Panel(const Rect& rect, Color color)
{
    DrawCommand command;
    command.kind = DrawKind::SolidRect;
    command.rect = rect;
    command.color = color;
    m_drawList.commands.push_back(std::move(command));
}

void Context::EmitText(const Rect& rect, const std::string& text, Color color,
                       Align hAlign, Align vAlign, float fontScale)
{
    DrawCommand command;
    command.kind = DrawKind::Text;
    command.rect = rect;
    command.color = color;
    command.text = text;
    command.hAlign = hAlign;
    command.vAlign = vAlign;
    command.fontScale = fontScale;
    m_drawList.commands.push_back(std::move(command));
}

void Context::Label(float x, float y, const std::string& text, Color color)
{
    // A rect anchored at (x, y); Start/Start alignment makes the renderer place
    // the text's top-left there and left-align it.
    EmitText(Rect{x, y, m_width - x, 0.0f}, text, color,
             Align::Start, Align::Start, m_style.fontScale);
}

bool Context::Button(std::uint32_t id, const Rect& rect, const std::string& text)
{
    const bool hovered = rect.Contains(m_input.pointerX, m_input.pointerY);

    // Capture the press so a click only counts when it both starts and ends on
    // the same button (the standard immediate-mode press/release rule).
    if (m_input.pointerPressed && hovered) {
        m_activePressId = id;
    }
    bool clicked = false;
    if (m_input.pointerReleased) {
        if (m_activePressId == id && hovered) {
            clicked = true;
        }
        if (m_activePressId == id) {
            m_activePressId = 0;
        }
    }

    const bool pressed = (m_activePressId == id) && m_input.pointerDown;
    const Color fill = pressed ? m_style.buttonActive
        : (hovered ? m_style.buttonHover : m_style.button);
    Panel(rect, fill);
    EmitText(rect, text, m_style.text, Align::Center, Align::Center, m_style.fontScale);

    return clicked;
}

void Context::EndFrame()
{
    // Reserved for finalizing layout cursors and the clip stack in later phases.
    // Kept explicit now so callers adopt the Begin/End bracket from the start.
}

void Context::Present(const UiDocument& document, float width, float height,
                      const Input& input)
{
    BeginFrame(width, height, input);
    m_clicked.clear();
    for (const Widget& widget : document.widgets) {
        switch (widget.kind) {
        case WidgetKind::Panel:
            Panel(widget.rect, widget.color);
            break;
        case WidgetKind::Label:
            EmitText(widget.rect, widget.text, widget.color,
                     widget.hAlign, widget.vAlign, widget.fontScale);
            break;
        case WidgetKind::Button:
            if (Button(widget.id, widget.rect, widget.text)) {
                m_clicked.push_back(widget.id);
            }
            break;
        }
    }
    EndFrame();
}

bool Context::Clicked(std::uint32_t id) const noexcept
{
    for (const std::uint32_t clickedId : m_clicked) {
        if (clickedId == id) {
            return true;
        }
    }
    return false;
}

} // namespace Concord::UI
