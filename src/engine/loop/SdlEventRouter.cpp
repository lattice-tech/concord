#include "engine/loop/SdlEventRouter.h"

#include "engine/debug/Logger.h"
#include "engine/events/EventPublishResult.h"
#include "engine/events/Events.h"
#include "engine/events/WindowInputEvents.h"
#include "engine/input/InputState.h"
#include "engine/loop/SdlInputMapping.h"

namespace Concord {

namespace {

const char* PublishResultName(EventPublishResult result)
{
    switch (result) {
        case EventPublishResult::Published: return "Published";
        case EventPublishResult::QueueFull: return "QueueFull";
        case EventPublishResult::Inactive: return "Inactive";
        case EventPublishResult::ShuttingDown: return "ShuttingDown";
    }
    return "Unknown";
}

} // namespace

template <typename T>
void SdlEventRouter::PublishCritical(const char* name, T&& event)
{
    const EventPublishResult result = Events::Publish(std::forward<T>(event));
    if (result != EventPublishResult::Published) {
        Debug::Logger::Error("Input", "failed to publish %s (%s)", name, PublishResultName(result));
    }
}

void SdlEventRouter::BeginFrame()
{
    for (auto& [id, source] : m_sources) {
        (void)id;
        source.motion = {};
        source.wheel = {};
    }
}

void SdlEventRouter::RegisterWindow(WindowId id, SDL_Window* window)
{
    if (id == kInvalidWindowId || window == nullptr) {
        return;
    }
    const SDL_WindowID sdlId = SDL_GetWindowID(window);
    if (sdlId == 0) {
        return;
    }

    // Drop any stale Concord mapping that still claims this SDL id.
    const auto existing = m_sdlToConcord.find(sdlId);
    if (existing != m_sdlToConcord.end() && existing->second != id) {
        UnregisterWindow(existing->second);
    }

    UnregisterWindow(id);
    SourceState source;
    source.sdlId = sdlId;
    source.window = window;
    m_sources.emplace(id, source);
    m_sdlToConcord[sdlId] = id;
}

void SdlEventRouter::UnregisterWindow(WindowId id)
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end()) {
        return;
    }
    ClearSource(id, it->second);
    m_sdlToConcord.erase(it->second.sdlId);
    m_sources.erase(it);
}

void SdlEventRouter::Reset()
{
    while (!m_sources.empty()) {
        UnregisterWindow(m_sources.begin()->first);
    }
    m_sdlToConcord.clear();
    InputState::Instance().Reset();
}

WindowId SdlEventRouter::FindWindow(SDL_WindowID sdlId) const
{
    const auto it = m_sdlToConcord.find(sdlId);
    return it != m_sdlToConcord.end() ? it->second : kInvalidWindowId;
}

WindowId SdlEventRouter::FindWindow(SDL_Window* window) const
{
    if (window == nullptr) {
        return kInvalidWindowId;
    }
    return FindWindow(SDL_GetWindowID(window));
}

WindowId SdlEventRouter::ResolveEventWindow(const SDL_Event& event) const
{
    if (SDL_Window* window = SDL_GetWindowFromEvent(&event)) {
        return FindWindow(window);
    }
    return kInvalidWindowId;
}

WindowId SdlEventRouter::Route(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const WindowId id = FindWindow(event.key.windowID);
            const Key key = KeyFromScancode(event.key.scancode);
            const bool down = event.type == SDL_EVENT_KEY_DOWN;
            const bool repeat = event.key.repeat;
            OnKey(id, key, down, repeat);
            return id;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const WindowId id = FindWindow(event.button.windowID);
            const MouseButton button = MouseButtonFromSdl(event.button.button);
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            OnMouseButton(id, button, down);
            return id;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            const WindowId id = FindWindow(event.motion.windowID);
            OnMouseMotion(id, event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
            return id;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            const WindowId id = FindWindow(event.wheel.windowID);
            OnMouseWheel(id, event.wheel.y);
            return id;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            const WindowId id = ResolveEventWindow(event);
            if (id != kInvalidWindowId) {
                OnFocus(id, event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
            }
            return id;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            const WindowId id = ResolveEventWindow(event);
            if (id != kInvalidWindowId) {
                NotifyCloseRequested(id);
            }
            return id;
        }
        default:
            return ResolveEventWindow(event);
    }
}

void SdlEventRouter::FlushCoalesced()
{
    for (auto& [id, source] : m_sources) {
        if (source.motion.dirty) {
            (void)Events::Publish(MouseMotionEvent{
                .window = id,
                .x = source.motion.x,
                .y = source.motion.y,
                .deltaX = source.motion.deltaX,
                .deltaY = source.motion.deltaY,
            });
            source.motion = {};
        }
        if (source.wheel.dirty) {
            (void)Events::Publish(MouseWheelEvent{
                .window = id,
                .delta = source.wheel.delta,
            });
            source.wheel = {};
        }
    }
}

void SdlEventRouter::NotifyCloseRequested(WindowId id)
{
    SourceState* source = FindSource(id);
    if (source == nullptr) {
        return;
    }
    if (source->closePublished) {
        return;
    }
    source->closePublished = true;
    // Handler runs after the OS window is closed this frame; id is still valid
    // as a notification token, not a live handle for further window ops.
    PublishCritical("WindowCloseRequestedEvent", WindowCloseRequestedEvent{.window = id});
    ClearSource(id, *source);
}

void SdlEventRouter::NotifyResized(WindowId id, int width, int height)
{
    if (id == kInvalidWindowId || width <= 0 || height <= 0) {
        return;
    }
    if (FindSource(id) == nullptr) {
        return;
    }
    PublishCritical("WindowResizedEvent", WindowResizedEvent{
        .window = id,
        .width = width,
        .height = height,
    });
}

void SdlEventRouter::OnKey(WindowId id, Key key, bool down, bool repeat)
{
    if (key == Key::Unknown || static_cast<std::size_t>(key) >= kKeyCount) {
        return;
    }

    InputState& input = InputState::Instance();
    SourceState* source = id != kInvalidWindowId ? FindSource(id) : nullptr;
    const auto index = static_cast<std::size_t>(key);

    if (down) {
        input.OnKeyDown(key);
        if (source != nullptr) {
            source->heldKeys[index] = true;
        }
    } else {
        // Physical release: device is up process-wide; drop every source's claim.
        ClearKeyOwnershipEverywhere(key);
        input.OnKeyUp(key);
    }

    if (id == kInvalidWindowId || source == nullptr) {
        return;
    }
    (void)Events::Publish(KeyEvent{
        .window = id,
        .key = key,
        .down = down,
        .repeat = repeat,
    });
}

void SdlEventRouter::OnMouseButton(WindowId id, MouseButton button, bool down)
{
    if (static_cast<std::size_t>(button) >= kButtonCount) {
        return;
    }

    InputState& input = InputState::Instance();
    SourceState* source = id != kInvalidWindowId ? FindSource(id) : nullptr;
    const auto index = static_cast<std::size_t>(button);

    if (down) {
        input.OnMouseButtonDown(button);
        if (source != nullptr) {
            source->heldButtons[index] = true;
        }
    } else {
        ClearButtonOwnershipEverywhere(button);
        input.OnMouseButtonUp(button);
    }

    if (id == kInvalidWindowId || source == nullptr) {
        return;
    }
    (void)Events::Publish(MouseButtonEvent{
        .window = id,
        .button = button,
        .down = down,
    });
}

void SdlEventRouter::OnMouseMotion(WindowId id, float x, float y, float dx, float dy)
{
    InputState::Instance().OnMouseMove(x, y, dx, dy);
    if (id == kInvalidWindowId) {
        return;
    }
    SourceState* source = FindSource(id);
    if (source == nullptr) {
        return;
    }
    source->motion.dirty = true;
    source->motion.x = x;
    source->motion.y = y;
    source->motion.deltaX += dx;
    source->motion.deltaY += dy;
}

void SdlEventRouter::OnMouseWheel(WindowId id, float delta)
{
    InputState::Instance().OnMouseWheel(delta);
    if (id == kInvalidWindowId) {
        return;
    }
    SourceState* source = FindSource(id);
    if (source == nullptr) {
        return;
    }
    source->wheel.dirty = true;
    source->wheel.delta += delta;
}

void SdlEventRouter::OnFocus(WindowId id, bool focused)
{
    SourceState* source = FindSource(id);
    if (source == nullptr) {
        return;
    }
    PublishCritical("WindowFocusChangedEvent", WindowFocusChangedEvent{
        .window = id,
        .focused = focused,
    });
    if (!focused) {
        ClearSource(id, *source);
    }
}

void SdlEventRouter::ClearSource(WindowId id, SourceState& source)
{
    (void)id;
    InputState& input = InputState::Instance();
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        if (!source.heldKeys[i]) {
            continue;
        }
        source.heldKeys[i] = false;
        const Key key = static_cast<Key>(i);
        if (!AnySourceHoldsKey(key)) {
            input.OnKeyUp(key);
        }
    }
    for (std::size_t i = 0; i < kButtonCount; ++i) {
        if (!source.heldButtons[i]) {
            continue;
        }
        source.heldButtons[i] = false;
        const MouseButton button = static_cast<MouseButton>(i);
        if (!AnySourceHoldsButton(button)) {
            input.OnMouseButtonUp(button);
        }
    }
    source.motion = {};
    source.wheel = {};
}

bool SdlEventRouter::AnySourceHoldsKey(Key key) const
{
    if (key == Key::Unknown || static_cast<std::size_t>(key) >= kKeyCount) {
        return false;
    }
    const auto index = static_cast<std::size_t>(key);
    for (const auto& [id, source] : m_sources) {
        (void)id;
        if (source.heldKeys[index]) {
            return true;
        }
    }
    return false;
}

bool SdlEventRouter::AnySourceHoldsButton(MouseButton button) const
{
    if (static_cast<std::size_t>(button) >= kButtonCount) {
        return false;
    }
    const auto index = static_cast<std::size_t>(button);
    for (const auto& [id, source] : m_sources) {
        (void)id;
        if (source.heldButtons[index]) {
            return true;
        }
    }
    return false;
}

void SdlEventRouter::ClearKeyOwnershipEverywhere(Key key)
{
    if (key == Key::Unknown || static_cast<std::size_t>(key) >= kKeyCount) {
        return;
    }
    const auto index = static_cast<std::size_t>(key);
    for (auto& [id, source] : m_sources) {
        (void)id;
        source.heldKeys[index] = false;
    }
}

void SdlEventRouter::ClearButtonOwnershipEverywhere(MouseButton button)
{
    if (static_cast<std::size_t>(button) >= kButtonCount) {
        return;
    }
    const auto index = static_cast<std::size_t>(button);
    for (auto& [id, source] : m_sources) {
        (void)id;
        source.heldButtons[index] = false;
    }
}

SdlEventRouter::SourceState* SdlEventRouter::FindSource(WindowId id)
{
    const auto it = m_sources.find(id);
    return it != m_sources.end() ? &it->second : nullptr;
}

} // namespace Concord
