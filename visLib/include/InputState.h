#pragma once

#include "Types.h"
#include <cstdint>

namespace visLib {

// Platform-independent key codes
// These are mapped to platform-specific codes (VK_*, GLFW, SDL, etc.) by the backend
enum class Key : uint32_t {
    Unknown = 0,

    // Letters (A-Z)
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Number row (0-9)
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Navigation
    Escape,
    Space,
    Enter,
    Tab,
    Backspace,
    Delete,
    Insert,

    // Arrow keys
    Left, Right, Up, Down,

    // Page navigation
    Home, End, PageUp, PageDown,

    // Modifiers
    LeftShift, RightShift,
    LeftCtrl, RightCtrl,
    LeftAlt, RightAlt,

    // Punctuation and symbols
    Comma,          // ,
    Period,         // .
    Slash,          // /
    Semicolon,      // ;
    Apostrophe,     // '
    LeftBracket,    // [
    RightBracket,   // ]
    Backslash,      // '\'
    Grave,          // `
    Minus,          // -
    Equals,         // =

    // Numpad
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal,
    NumpadEnter,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,

    // Mouse buttons
    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseX1,
    MouseX2,

    // Count for iteration/array sizing
    Count
};

// Abstract input state interface
// Provides platform-independent access to keyboard, mouse, and gamepad state
class InputState {
public:
    virtual ~InputState() = default;

    // Keyboard and mouse button queries
    // isKeyDown: returns true if the key is currently held down
    virtual bool isKeyDown(Key key) const = 0;

    // isKeyPressed: returns true only on the frame when the key was first pressed
    virtual bool isKeyPressed(Key key) const = 0;

    // isKeyReleased: returns true only on the frame when the key was released
    virtual bool isKeyReleased(Key key) const = 0;

    // Mouse position in window coordinates (pixels from top-left)
    virtual float2 getMousePosition() const = 0;

    // Mouse movement since last frame
    virtual float2 getMouseDelta() const = 0;

    // Scroll wheel delta since last frame (positive = scroll up)
    virtual float getScrollDelta() const = 0;
};

} // namespace visLib
