#include "engine/input/InputState.h"

namespace Concord {

namespace {

struct SimulationInputSnapshot {
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> pressed{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> released{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> buttons{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> buttonPressed{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> buttonReleased{};
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float wheel = 0.0f;
    bool active = false;
};

thread_local SimulationInputSnapshot g_simulationInput;

/** True when `key` is a real, in-range key (not Unknown/Count). */
bool IsUsable(Key key)
{
    const auto index = static_cast<std::size_t>(key);
    return key != Key::Unknown && index < static_cast<std::size_t>(Key::Count);
}

bool IsUsable(MouseButton button)
{
    return static_cast<std::size_t>(button) < static_cast<std::size_t>(MouseButton::Count);
}

} // namespace

InputState& InputState::Instance()
{
    static InputState instance;
    return instance;
}

void InputState::BeginFrame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keysPrev = m_keys;
    m_buttonsPrev = m_buttons;
    // Deltas accumulate over a single frame's worth of events, so clear them
    // here before this frame's events arrive.
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheel = 0.0f;
}

void InputState::BeginSimulationFrame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    g_simulationInput.keys = m_keys;
    g_simulationInput.pressed = m_pendingKeyPressed;
    g_simulationInput.released = m_pendingKeyReleased;
    g_simulationInput.buttons = m_buttons;
    g_simulationInput.buttonPressed = m_pendingButtonPressed;
    g_simulationInput.buttonReleased = m_pendingButtonReleased;
    g_simulationInput.mouseX = m_mouseX;
    g_simulationInput.mouseY = m_mouseY;
    g_simulationInput.mouseDeltaX = m_pendingMouseDeltaX;
    g_simulationInput.mouseDeltaY = m_pendingMouseDeltaY;
    g_simulationInput.wheel = m_pendingWheel;
    g_simulationInput.active = true;
    m_pendingKeyPressed.fill(false);
    m_pendingKeyReleased.fill(false);
    m_pendingButtonPressed.fill(false);
    m_pendingButtonReleased.fill(false);
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_pendingWheel = 0.0f;
}

void InputState::OnKeyDown(Key key)
{
    if (!IsUsable(key)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t index = static_cast<std::size_t>(key);
    if (!m_keys[index]) {
        m_pendingKeyPressed[index] = true;
    }
    m_keys[index] = true;
}

void InputState::OnKeyUp(Key key)
{
    if (!IsUsable(key)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t index = static_cast<std::size_t>(key);
    if (m_keys[index]) {
        m_pendingKeyReleased[index] = true;
    }
    m_keys[index] = false;
}

void InputState::OnMouseMove(float x, float y, float dx, float dy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mouseX = x;
    m_mouseY = y;
    m_mouseDeltaX += dx;
    m_mouseDeltaY += dy;
    m_pendingMouseDeltaX += dx;
    m_pendingMouseDeltaY += dy;
}

void InputState::OnMouseButtonDown(MouseButton button)
{
    if (!IsUsable(button)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t index = static_cast<std::size_t>(button);
    if (!m_buttons[index]) {
        m_pendingButtonPressed[index] = true;
    }
    m_buttons[index] = true;
}

void InputState::OnMouseButtonUp(MouseButton button)
{
    if (!IsUsable(button)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t index = static_cast<std::size_t>(button);
    if (m_buttons[index]) {
        m_pendingButtonReleased[index] = true;
    }
    m_buttons[index] = false;
}

void InputState::OnMouseWheel(float delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wheel += delta;
    m_pendingWheel += delta;
}

void InputState::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keys.fill(false);
    m_keysPrev.fill(false);
    m_buttons.fill(false);
    m_buttonsPrev.fill(false);
    m_pendingKeyPressed.fill(false);
    m_pendingKeyReleased.fill(false);
    m_pendingButtonPressed.fill(false);
    m_pendingButtonReleased.fill(false);
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheel = 0.0f;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_pendingWheel = 0.0f;
}

bool InputState::IsKeyDown(Key key) const
{
    if (!IsUsable(key)) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(key);
    if (g_simulationInput.active) {
        return g_simulationInput.keys[index];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keys[index];
}

bool InputState::WasKeyPressed(Key key) const
{
    if (!IsUsable(key)) {
        return false;
    }
    const auto i = static_cast<std::size_t>(key);
    if (g_simulationInput.active) {
        return g_simulationInput.pressed[i];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keys[i] && !m_keysPrev[i];
}

bool InputState::WasKeyReleased(Key key) const
{
    if (!IsUsable(key)) {
        return false;
    }
    const auto i = static_cast<std::size_t>(key);
    if (g_simulationInput.active) {
        return g_simulationInput.released[i];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_keys[i] && m_keysPrev[i];
}

bool InputState::IsMouseButtonDown(MouseButton button) const
{
    if (!IsUsable(button)) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(button);
    if (g_simulationInput.active) {
        return g_simulationInput.buttons[index];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buttons[index];
}

bool InputState::WasMouseButtonPressed(MouseButton button) const
{
    if (!IsUsable(button)) {
        return false;
    }
    const auto i = static_cast<std::size_t>(button);
    if (g_simulationInput.active) {
        return g_simulationInput.buttonPressed[i];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buttons[i] && !m_buttonsPrev[i];
}

bool InputState::WasMouseButtonReleased(MouseButton button) const
{
    if (!IsUsable(button)) {
        return false;
    }
    const auto i = static_cast<std::size_t>(button);
    if (g_simulationInput.active) {
        return g_simulationInput.buttonReleased[i];
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_buttons[i] && m_buttonsPrev[i];
}

void InputState::MousePosition(float& x, float& y) const
{
    if (g_simulationInput.active) {
        x = g_simulationInput.mouseX;
        y = g_simulationInput.mouseY;
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    x = m_mouseX;
    y = m_mouseY;
}

void InputState::MouseDelta(float& dx, float& dy) const
{
    if (g_simulationInput.active) {
        dx = g_simulationInput.mouseDeltaX;
        dy = g_simulationInput.mouseDeltaY;
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    dx = m_mouseDeltaX;
    dy = m_mouseDeltaY;
}

float InputState::MouseWheel() const
{
    if (g_simulationInput.active) {
        return g_simulationInput.wheel;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_wheel;
}

} // namespace Concord
