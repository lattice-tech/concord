#ifndef CONCORD_SDLEVENTROUTER_H
#define CONCORD_SDLEVENTROUTER_H

#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"
#include "engine/window/WindowId.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Concord {

/**
 * @brief Single SDL → Concord conversion path for polling input and typed events.
 *
 * Render-thread only. Each frame: BeginFrame, Route every polled SDL event,
 * then FlushCoalesced before EventBus dispatch. Process-wide `InputState` and
 * typed window/input notifications share this path so sources stay aligned.
 *
 * Held keys / buttons are owned by the window that produced them. Focus loss,
 * close, detach and shutdown clear only that source; process-wide polling is
 * released only when no remaining source still owns the key or button.
 * Physical KEY_UP / button-up clears ownership on every source (the device
 * state is process-wide).
 */
class SdlEventRouter {
public:
    /** Clears per-frame motion and wheel accumulators; call once before PollEvent. */
    void BeginFrame();

    /** Binds an open SDL window to a Concord WindowId for event attribution. */
    void RegisterWindow(WindowId id, SDL_Window* window);

    /**
     * Drops the mapping for `id`, releases its held keys/buttons into InputState
     * when no other source still owns them, and discards staged motion / wheel.
     */
    void UnregisterWindow(WindowId id);

    /**
     * Normalizes one SDL event: updates process-wide InputState and stages or
     * publishes typed notifications. Returns the Concord window that owned the
     * event when resolvable, otherwise kInvalidWindowId.
     */
    WindowId Route(const SDL_Event& event);

    /**
     * Publishes coalesced per-window mouse motion and wheel for this frame.
     * Call after the SDL poll loop and after resize / close normalization,
     * before EventBusCore::Dispatch.
     */
    void FlushCoalesced();

    /**
     * Publishes a close-requested notification for `id` once, then clears that
     * source's held input. Safe to call multiple times; only the first publishes.
     */
    void NotifyCloseRequested(WindowId id);

    /**
     * Publishes a resized notification with the final positive framebuffer pixel
     * size. Zero sizes are ignored.
     */
    void NotifyResized(WindowId id, int width, int height);

    /** Releases every tracked source (loop shutdown). */
    void Reset();

    /** Resolves an SDL window id to Concord's WindowId, or kInvalidWindowId. */
    WindowId FindWindow(SDL_WindowID sdlId) const;

    /** Resolves an SDL_Window pointer to Concord's WindowId, or kInvalidWindowId. */
    WindowId FindWindow(SDL_Window* window) const;

private:
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::Count);

    struct MotionAccum {
        bool dirty = false;
        float x = 0.0f;
        float y = 0.0f;
        float deltaX = 0.0f;
        float deltaY = 0.0f;
    };

    struct WheelAccum {
        bool dirty = false;
        float delta = 0.0f;
    };

    struct SourceState {
        SDL_WindowID sdlId = 0;
        SDL_Window* window = nullptr;
        std::array<bool, kKeyCount> heldKeys{};
        std::array<bool, kButtonCount> heldButtons{};
        MotionAccum motion{};
        WheelAccum wheel{};
        bool closePublished = false;
    };

    void OnKey(WindowId id, Key key, bool down, bool repeat);
    void OnMouseButton(WindowId id, MouseButton button, bool down);
    void OnMouseMotion(WindowId id, float x, float y, float dx, float dy);
    void OnMouseWheel(WindowId id, float delta);
    void OnFocus(WindowId id, bool focused);
    void ClearSource(WindowId id, SourceState& source);
    bool AnySourceHoldsKey(Key key) const;
    bool AnySourceHoldsButton(MouseButton button) const;
    void ClearKeyOwnershipEverywhere(Key key);
    void ClearButtonOwnershipEverywhere(MouseButton button);
    SourceState* FindSource(WindowId id);
    WindowId ResolveEventWindow(const SDL_Event& event) const;

    template <typename T>
    void PublishCritical(const char* name, T&& event);

    std::unordered_map<WindowId, SourceState> m_sources;
    std::unordered_map<SDL_WindowID, WindowId> m_sdlToConcord;
};

} // namespace Concord

#endif // CONCORD_SDLEVENTROUTER_H
