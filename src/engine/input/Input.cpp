#include "engine/input/Input.h"

#include "engine/input/InputState.h"

namespace Concord {

bool Input::IsKeyDown(Key key)
{
    return InputState::Instance().IsKeyDown(key);
}

bool Input::WasKeyPressed(Key key)
{
    return InputState::Instance().WasKeyPressed(key);
}

bool Input::WasKeyReleased(Key key)
{
    return InputState::Instance().WasKeyReleased(key);
}

bool Input::IsMouseButtonDown(MouseButton button)
{
    return InputState::Instance().IsMouseButtonDown(button);
}

bool Input::WasMouseButtonPressed(MouseButton button)
{
    return InputState::Instance().WasMouseButtonPressed(button);
}

bool Input::WasMouseButtonReleased(MouseButton button)
{
    return InputState::Instance().WasMouseButtonReleased(button);
}

float Input::MouseX()
{
    float x = 0.0f;
    float y = 0.0f;
    InputState::Instance().MousePosition(x, y);
    return x;
}

float Input::MouseY()
{
    float x = 0.0f;
    float y = 0.0f;
    InputState::Instance().MousePosition(x, y);
    return y;
}

float Input::MouseDeltaX()
{
    float dx = 0.0f;
    float dy = 0.0f;
    InputState::Instance().MouseDelta(dx, dy);
    return dx;
}

float Input::MouseDeltaY()
{
    float dx = 0.0f;
    float dy = 0.0f;
    InputState::Instance().MouseDelta(dx, dy);
    return dy;
}

float Input::MouseWheel()
{
    return InputState::Instance().MouseWheel();
}

} // namespace Concord
