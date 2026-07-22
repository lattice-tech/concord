#include "engine/loop/SdlInputMapping.h"

namespace Concord {

Key KeyFromScancode(SDL_Scancode scancode)
{
    switch (scancode) {
        case SDL_SCANCODE_A: return Key::A;
        case SDL_SCANCODE_B: return Key::B;
        case SDL_SCANCODE_C: return Key::C;
        case SDL_SCANCODE_D: return Key::D;
        case SDL_SCANCODE_E: return Key::E;
        case SDL_SCANCODE_F: return Key::F;
        case SDL_SCANCODE_G: return Key::G;
        case SDL_SCANCODE_H: return Key::H;
        case SDL_SCANCODE_I: return Key::I;
        case SDL_SCANCODE_J: return Key::J;
        case SDL_SCANCODE_K: return Key::K;
        case SDL_SCANCODE_L: return Key::L;
        case SDL_SCANCODE_M: return Key::M;
        case SDL_SCANCODE_N: return Key::N;
        case SDL_SCANCODE_O: return Key::O;
        case SDL_SCANCODE_P: return Key::P;
        case SDL_SCANCODE_Q: return Key::Q;
        case SDL_SCANCODE_R: return Key::R;
        case SDL_SCANCODE_S: return Key::S;
        case SDL_SCANCODE_T: return Key::T;
        case SDL_SCANCODE_U: return Key::U;
        case SDL_SCANCODE_V: return Key::V;
        case SDL_SCANCODE_W: return Key::W;
        case SDL_SCANCODE_X: return Key::X;
        case SDL_SCANCODE_Y: return Key::Y;
        case SDL_SCANCODE_Z: return Key::Z;

        case SDL_SCANCODE_0: return Key::Num0;
        case SDL_SCANCODE_1: return Key::Num1;
        case SDL_SCANCODE_2: return Key::Num2;
        case SDL_SCANCODE_3: return Key::Num3;
        case SDL_SCANCODE_4: return Key::Num4;
        case SDL_SCANCODE_5: return Key::Num5;
        case SDL_SCANCODE_6: return Key::Num6;
        case SDL_SCANCODE_7: return Key::Num7;
        case SDL_SCANCODE_8: return Key::Num8;
        case SDL_SCANCODE_9: return Key::Num9;

        case SDL_SCANCODE_F1:  return Key::F1;
        case SDL_SCANCODE_F2:  return Key::F2;
        case SDL_SCANCODE_F3:  return Key::F3;
        case SDL_SCANCODE_F4:  return Key::F4;
        case SDL_SCANCODE_F5:  return Key::F5;
        case SDL_SCANCODE_F6:  return Key::F6;
        case SDL_SCANCODE_F7:  return Key::F7;
        case SDL_SCANCODE_F8:  return Key::F8;
        case SDL_SCANCODE_F9:  return Key::F9;
        case SDL_SCANCODE_F10: return Key::F10;
        case SDL_SCANCODE_F11: return Key::F11;
        case SDL_SCANCODE_F12: return Key::F12;

        case SDL_SCANCODE_ESCAPE:    return Key::Escape;
        case SDL_SCANCODE_RETURN:    return Key::Enter;
        case SDL_SCANCODE_TAB:       return Key::Tab;
        case SDL_SCANCODE_SPACE:     return Key::Space;
        case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
        case SDL_SCANCODE_DELETE:    return Key::Delete;
        case SDL_SCANCODE_INSERT:    return Key::Insert;
        case SDL_SCANCODE_HOME:      return Key::Home;
        case SDL_SCANCODE_END:       return Key::End;
        case SDL_SCANCODE_PAGEUP:    return Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN:  return Key::PageDown;

        case SDL_SCANCODE_LEFT:  return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        case SDL_SCANCODE_UP:    return Key::Up;
        case SDL_SCANCODE_DOWN:  return Key::Down;

        case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
        case SDL_SCANCODE_RSHIFT: return Key::RightShift;
        case SDL_SCANCODE_LCTRL:  return Key::LeftControl;
        case SDL_SCANCODE_RCTRL:  return Key::RightControl;
        case SDL_SCANCODE_LALT:   return Key::LeftAlt;
        case SDL_SCANCODE_RALT:   return Key::RightAlt;
        case SDL_SCANCODE_LGUI:   return Key::LeftSuper;
        case SDL_SCANCODE_RGUI:   return Key::RightSuper;
        case SDL_SCANCODE_CAPSLOCK: return Key::CapsLock;

        case SDL_SCANCODE_MINUS:        return Key::Minus;
        case SDL_SCANCODE_EQUALS:       return Key::Equals;
        case SDL_SCANCODE_LEFTBRACKET:  return Key::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return Key::RightBracket;
        case SDL_SCANCODE_BACKSLASH:    return Key::Backslash;
        case SDL_SCANCODE_SEMICOLON:    return Key::Semicolon;
        case SDL_SCANCODE_APOSTROPHE:   return Key::Apostrophe;
        case SDL_SCANCODE_GRAVE:        return Key::Grave;
        case SDL_SCANCODE_COMMA:        return Key::Comma;
        case SDL_SCANCODE_PERIOD:       return Key::Period;
        case SDL_SCANCODE_SLASH:        return Key::Slash;

        case SDL_SCANCODE_KP_0: return Key::Keypad0;
        case SDL_SCANCODE_KP_1: return Key::Keypad1;
        case SDL_SCANCODE_KP_2: return Key::Keypad2;
        case SDL_SCANCODE_KP_3: return Key::Keypad3;
        case SDL_SCANCODE_KP_4: return Key::Keypad4;
        case SDL_SCANCODE_KP_5: return Key::Keypad5;
        case SDL_SCANCODE_KP_6: return Key::Keypad6;
        case SDL_SCANCODE_KP_7: return Key::Keypad7;
        case SDL_SCANCODE_KP_8: return Key::Keypad8;
        case SDL_SCANCODE_KP_9: return Key::Keypad9;
        case SDL_SCANCODE_KP_PLUS:     return Key::KeypadPlus;
        case SDL_SCANCODE_KP_MINUS:    return Key::KeypadMinus;
        case SDL_SCANCODE_KP_MULTIPLY: return Key::KeypadMultiply;
        case SDL_SCANCODE_KP_DIVIDE:   return Key::KeypadDivide;
        case SDL_SCANCODE_KP_ENTER:    return Key::KeypadEnter;
        case SDL_SCANCODE_KP_PERIOD:   return Key::KeypadPeriod;

        default: return Key::Unknown;
    }
}

MouseButton MouseButtonFromSdl(std::uint8_t button)
{
    switch (button) {
        case SDL_BUTTON_LEFT:   return MouseButton::Left;
        case SDL_BUTTON_RIGHT:  return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1:     return MouseButton::X1;
        case SDL_BUTTON_X2:     return MouseButton::X2;
        default:                return MouseButton::Count;
    }
}

} // namespace Concord
