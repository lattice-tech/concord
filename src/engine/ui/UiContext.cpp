#include "engine/ui/UiContext.h"

#include <algorithm>
#include <cmath>
#include <iterator>
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
    m_clicked.clear();
    m_focusOrder.clear();
    m_seenIds.clear();
    m_feedback.clear();
    m_activeSeen = false;
    m_wantsPointer = false;
    m_cancelled = input.cancel;
    m_wantsKeyboard = m_focusedId != 0u || input.focusNext
        || input.focusPrevious || input.activate || input.cancel;

    if (!input.pointerValid || input.cancel) {
        m_activePressId = 0u;
    }
    m_wantsPointer = input.pointerValid && m_activePressId != 0u;
    if (input.focusNext != input.focusPrevious) {
        MoveFocus(input.focusPrevious);
    }
}

void Context::EmitSolidRect(const Rect& rect, Color color)
{
    DrawCommand command;
    command.kind = DrawKind::SolidRect;
    command.rect = rect;
    command.color = color;
    m_drawList.commands.push_back(std::move(command));
}

void Context::Panel(const Rect& rect, Color color)
{
    EmitSolidRect(rect, color);
    if (m_input.pointerValid && rect.Contains(m_input.pointerX, m_input.pointerY)) {
        m_wantsPointer = true;
    }
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
    return ButtonWithFeedback(id, rect, text).activated;
}

bool Context::Button(std::uint32_t id, const Rect& rect, const std::string& text,
                     const ButtonStyle& style, bool enabled)
{
    return ButtonWithFeedback(id, rect, text, style, enabled).activated;
}

WidgetFeedback Context::ButtonWithFeedback(std::uint32_t id, const Rect& rect,
                                            const std::string& text, bool enabled)
{
    return ButtonWithFeedback(id, rect, text, ButtonStyleFrom(m_style), enabled);
}

WidgetFeedback Context::ButtonWithFeedback(std::uint32_t id, const Rect& rect,
                                            const std::string& text,
                                            const ButtonStyle& style, bool enabled)
{
    const bool duplicate = id != 0u && WasSeen(id);
    if (id != 0u && !duplicate) {
        m_seenIds.push_back(id);
    }
    const bool interactive = enabled && id != 0u && !duplicate;
    const bool hovered = m_input.pointerValid
        && rect.Contains(m_input.pointerX, m_input.pointerY);
    if (hovered || (interactive && m_activePressId == id)) {
        m_wantsPointer = true;
    }

    if (interactive) {
        m_focusOrder.push_back(id);
        if (m_input.pointerPressed && hovered) {
            m_activePressId = id;
            m_focusedId = id;
        }
        if (m_activePressId == id) {
            m_activeSeen = true;
        }
    }

    WidgetFeedback feedback;
    feedback.hovered = hovered;
    feedback.disabled = !interactive;
    feedback.focused = interactive && m_focusedId == id;
    feedback.pressed = interactive && m_activePressId == id && m_input.pointerDown;

    if (interactive && m_input.pointerReleased && m_activePressId == id) {
        feedback.activated = hovered;
        m_activePressId = 0u;
    }
    if (feedback.focused && m_input.activate) {
        feedback.activated = true;
    }
    if (feedback.focused) {
        m_wantsKeyboard = true;
    }

    const Color fill = feedback.disabled ? style.disabled
        : feedback.pressed ? style.active
        : feedback.hovered ? style.hover : style.fill;
    EmitSolidRect(rect, fill);
    if (feedback.focused) {
        DrawFocusBorder(rect, style.focus, style.focusThickness);
    }
    EmitText(rect, text, feedback.disabled ? style.textDisabled : style.text,
             Align::Center, Align::Center, style.fontScale);

    if (feedback.activated) {
        m_clicked.push_back(id);
    }
    if (id != 0u && !duplicate) {
        StoreFeedback(id, feedback);
    }
    return feedback;
}

void Context::EndFrame()
{
    if (m_activePressId != 0u && !m_activeSeen) {
        m_activePressId = 0u;
    }

    const auto focused = std::find(m_focusOrder.begin(), m_focusOrder.end(), m_focusedId);
    if (m_focusedId != 0u && focused == m_focusOrder.end()) {
        m_focusedId = 0u;
    }
    if (m_pendingInitialFocus && !m_focusOrder.empty()) {
        m_focusedId = m_pendingInitialFocusBackwards
            ? m_focusOrder.back() : m_focusOrder.front();
    }
    m_pendingInitialFocus = false;
    m_pendingInitialFocusBackwards = false;
    m_wantsKeyboard = m_wantsKeyboard || m_focusedId != 0u;
    m_previousFocusOrder = std::move(m_focusOrder);
    m_focusOrder.clear();
}

void Context::Present(const UiDocument& document, float width, float height,
                      const Input& input)
{
    BeginFrame(width, height, input);
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
            (void)Button(widget.id, widget.rect, widget.text);
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

WidgetFeedback Context::GetFeedback(std::uint32_t id) const noexcept
{
    for (const FeedbackEntry& entry : m_feedback) {
        if (entry.id == id) {
            return entry.feedback;
        }
    }
    return {};
}

void Context::DrawFocusBorder(const Rect& rect, Color color, float requestedThickness)
{
    if (!std::isfinite(requestedThickness) || requestedThickness <= 0.0f
        || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const float thickness = std::min(
        requestedThickness, std::min(rect.width, rect.height) * 0.5f);
    EmitSolidRect(Rect{rect.x, rect.y, rect.width, thickness}, color);
    EmitSolidRect(Rect{rect.x, rect.y + rect.height - thickness,
                       rect.width, thickness}, color);
    EmitSolidRect(Rect{rect.x, rect.y + thickness, thickness,
                       rect.height - thickness * 2.0f}, color);
    EmitSolidRect(Rect{rect.x + rect.width - thickness, rect.y + thickness,
                       thickness, rect.height - thickness * 2.0f}, color);
}

void Context::MoveFocus(bool backwards)
{
    if (m_previousFocusOrder.empty()) {
        m_pendingInitialFocus = true;
        m_pendingInitialFocusBackwards = backwards;
        return;
    }

    const auto current = std::find(
        m_previousFocusOrder.begin(), m_previousFocusOrder.end(), m_focusedId);
    if (current == m_previousFocusOrder.end()) {
        m_focusedId = backwards ? m_previousFocusOrder.back()
                                : m_previousFocusOrder.front();
        return;
    }
    const std::size_t index = static_cast<std::size_t>(
        std::distance(m_previousFocusOrder.begin(), current));
    if (backwards) {
        m_focusedId = m_previousFocusOrder[
            index == 0u ? m_previousFocusOrder.size() - 1u : index - 1u];
    } else {
        m_focusedId = m_previousFocusOrder[(index + 1u) % m_previousFocusOrder.size()];
    }
}

void Context::StoreFeedback(std::uint32_t id, const WidgetFeedback& feedback)
{
    m_feedback.push_back(FeedbackEntry{id, feedback});
}

bool Context::WasSeen(std::uint32_t id) const noexcept
{
    return std::find(m_seenIds.begin(), m_seenIds.end(), id) != m_seenIds.end();
}

} // namespace Concord::UI
