#ifndef CONCORD_INPUTSTATE_H
#define CONCORD_INPUTSTATE_H

#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"

#include <array>
#include <cstddef>
#include <mutex>

namespace Concord {

/**
 * Process-wide keyboard and mouse state, fed by the engine's render thread
 * from platform events and queried through Concord::Input.
 *
 * Held state is updated by the render thread. Edges and pointer deltas are
 * accumulated until BeginSimulationFrame atomically publishes one immutable
 * simulation-thread snapshot, so a slow simulation turn cannot lose input.
 * External readers remain thread-safe and observe render-frame state.
 *
 * There is one keyboard and mouse per process, so this is a singleton
 * (Instance), mirroring the process-wide EngineLoop that drives it.
 */
class InputState {
public:
    /** The process-wide instance. */
    static InputState& Instance();

    // —— Fed by the render thread from platform events ——

    /** Rolls current state into previous and clears per-frame deltas; once per frame, before events. */
    void BeginFrame();

    /** Captures and consumes accumulated input edges for one simulation turn. */
    void BeginSimulationFrame();

    void OnKeyDown(Key key);
    void OnKeyUp(Key key);
    void OnMouseMove(float x, float y, float dx, float dy);
    void OnMouseButtonDown(MouseButton button);
    void OnMouseButtonUp(MouseButton button);
    void OnMouseWheel(float delta);

    /** Clears all state (e.g. on focus loss); leaves no keys stuck down. */
    void Reset();

    // —— Queried through Concord::Input ——

    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;
    bool WasKeyReleased(Key key) const;

    bool IsMouseButtonDown(MouseButton button) const;
    bool WasMouseButtonPressed(MouseButton button) const;
    bool WasMouseButtonReleased(MouseButton button) const;

    void MousePosition(float& x, float& y) const;
    void MouseDelta(float& dx, float& dy) const;
    float MouseWheel() const;

private:
    InputState() = default;

    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::Count);

    mutable std::mutex m_mutex;

    std::array<bool, kKeyCount> m_keys{};
    std::array<bool, kKeyCount> m_keysPrev{};
    std::array<bool, kKeyCount> m_pendingKeyPressed{};
    std::array<bool, kKeyCount> m_pendingKeyReleased{};
    std::array<bool, kButtonCount> m_buttons{};
    std::array<bool, kButtonCount> m_buttonsPrev{};
    std::array<bool, kButtonCount> m_pendingButtonPressed{};
    std::array<bool, kButtonCount> m_pendingButtonReleased{};

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    float m_wheel = 0.0f;
    float m_pendingMouseDeltaX = 0.0f;
    float m_pendingMouseDeltaY = 0.0f;
    float m_pendingWheel = 0.0f;
};

} // namespace Concord

#endif // CONCORD_INPUTSTATE_H
