#ifndef CONCORD_KEY_H
#define CONCORD_KEY_H

#include <cstdint>

namespace Concord {

/**
 * A physical keyboard key, identified by position (like a scancode), not by
 * the character it produces — so `Key::W` is the same physical key regardless
 * of keyboard layout.
 *
 * The set is curated to the keys games commonly bind; the platform layer maps
 * its native scancodes onto these, and anything unmapped surfaces as
 * `Key::Unknown`. `Count` is a sentinel sizing the internal state arrays and is
 * never a real key.
 */
enum class Key : std::uint16_t {
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Escape, Enter, Tab, Space, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,

    Left, Right, Up, Down,

    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    LeftSuper, RightSuper, CapsLock,

    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon,
    Apostrophe, Grave, Comma, Period, Slash,

    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadPlus, KeypadMinus, KeypadMultiply, KeypadDivide, KeypadEnter, KeypadPeriod,

    Count,
};

} // namespace Concord

#endif // CONCORD_KEY_H
