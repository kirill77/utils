#ifdef _WIN32

#include "Win32InputState.h"

namespace visLib {

Win32InputState::Win32InputState()
    : m_mousePosition(0.0f, 0.0f)
    , m_mouseDelta(0.0f, 0.0f)
    , m_lastMousePosition(0.0f, 0.0f)
    , m_scrollDelta(0.0f)
    , m_firstFrame(true)
{
    m_currentState.fill(false);
    m_previousState.fill(false);
}

bool Win32InputState::isKeyDown(Key key) const
{
    if (key == Key::Unknown || key >= Key::Count) return false;
    return m_currentState[static_cast<size_t>(key)];
}

bool Win32InputState::isKeyPressed(Key key) const
{
    if (key == Key::Unknown || key >= Key::Count) return false;
    size_t idx = static_cast<size_t>(key);
    return m_currentState[idx] && !m_previousState[idx];
}

bool Win32InputState::isKeyReleased(Key key) const
{
    if (key == Key::Unknown || key >= Key::Count) return false;
    size_t idx = static_cast<size_t>(key);
    return !m_currentState[idx] && m_previousState[idx];
}

float2 Win32InputState::getMousePosition() const
{
    return m_mousePosition;
}

float2 Win32InputState::getMouseDelta() const
{
    return m_mouseDelta;
}

float Win32InputState::getScrollDelta() const
{
    return m_scrollDelta;
}

void Win32InputState::beginFrame()
{
    // Save current state as previous
    m_previousState = m_currentState;
    
    // Calculate mouse delta
    if (m_firstFrame)
    {
        m_mouseDelta = float2(0.0f, 0.0f);
        m_firstFrame = false;
    }
    else
    {
        m_mouseDelta = m_mousePosition - m_lastMousePosition;
    }
    m_lastMousePosition = m_mousePosition;
    
    // Reset per-frame values
    m_scrollDelta = 0.0f;
}

void Win32InputState::endFrame()
{
    // Nothing to do here currently
}

void Win32InputState::onKeyDown(WPARAM vkCode)
{
    Key key = vkToKey(vkCode);
    if (key != Key::Unknown && key < Key::Count)
    {
        m_currentState[static_cast<size_t>(key)] = true;
    }
}

void Win32InputState::onKeyUp(WPARAM vkCode)
{
    Key key = vkToKey(vkCode);
    if (key != Key::Unknown && key < Key::Count)
    {
        m_currentState[static_cast<size_t>(key)] = false;
    }
}

void Win32InputState::onMouseMove(int x, int y)
{
    m_mousePosition = float2(static_cast<float>(x), static_cast<float>(y));
}

void Win32InputState::onMouseButton(Key button, bool down)
{
    if (button >= Key::MouseLeft && button <= Key::MouseX2)
    {
        m_currentState[static_cast<size_t>(button)] = down;
    }
}

void Win32InputState::onMouseWheel(float delta)
{
    m_scrollDelta += delta;
}

Key Win32InputState::vkToKey(WPARAM vkCode)
{
    // Letters A-Z
    if (vkCode >= 'A' && vkCode <= 'Z')
    {
        return static_cast<Key>(static_cast<uint32_t>(Key::A) + (vkCode - 'A'));
    }
    
    // Numbers 0-9
    if (vkCode >= '0' && vkCode <= '9')
    {
        return static_cast<Key>(static_cast<uint32_t>(Key::Num0) + (vkCode - '0'));
    }
    
    // Function keys F1-F12
    if (vkCode >= VK_F1 && vkCode <= VK_F12)
    {
        return static_cast<Key>(static_cast<uint32_t>(Key::F1) + (vkCode - VK_F1));
    }
    
    // Numpad 0-9
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9)
    {
        return static_cast<Key>(static_cast<uint32_t>(Key::Numpad0) + (vkCode - VK_NUMPAD0));
    }
    
    // Other keys
    switch (vkCode)
    {
        case VK_ESCAPE:     return Key::Escape;
        case VK_SPACE:      return Key::Space;
        case VK_RETURN:     return Key::Enter;
        case VK_TAB:        return Key::Tab;
        case VK_BACK:       return Key::Backspace;
        case VK_DELETE:     return Key::Delete;
        case VK_INSERT:     return Key::Insert;
        
        case VK_LEFT:       return Key::Left;
        case VK_RIGHT:      return Key::Right;
        case VK_UP:         return Key::Up;
        case VK_DOWN:       return Key::Down;
        
        case VK_HOME:       return Key::Home;
        case VK_END:        return Key::End;
        case VK_PRIOR:      return Key::PageUp;
        case VK_NEXT:       return Key::PageDown;
        
        case VK_SHIFT:      return Key::LeftShift;   // Generic Shift
        case VK_LSHIFT:     return Key::LeftShift;
        case VK_RSHIFT:     return Key::RightShift;
        case VK_CONTROL:    return Key::LeftCtrl;    // Generic Ctrl
        case VK_LCONTROL:   return Key::LeftCtrl;
        case VK_RCONTROL:   return Key::RightCtrl;
        case VK_MENU:       return Key::LeftAlt;     // Generic Alt
        case VK_LMENU:      return Key::LeftAlt;
        case VK_RMENU:      return Key::RightAlt;
        
        case VK_OEM_COMMA:  return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period;
        case VK_OEM_2:      return Key::Slash;
        case VK_OEM_1:      return Key::Semicolon;
        case VK_OEM_7:      return Key::Apostrophe;
        case VK_OEM_4:      return Key::LeftBracket;
        case VK_OEM_6:      return Key::RightBracket;
        case VK_OEM_5:      return Key::Backslash;
        case VK_OEM_3:      return Key::Grave;
        case VK_OEM_MINUS:  return Key::Minus;
        case VK_OEM_PLUS:   return Key::Equals;
        
        case VK_DECIMAL:    return Key::NumpadDecimal;
        case VK_ADD:        return Key::NumpadAdd;
        case VK_SUBTRACT:   return Key::NumpadSubtract;
        case VK_MULTIPLY:   return Key::NumpadMultiply;
        case VK_DIVIDE:     return Key::NumpadDivide;
        
        default:            return Key::Unknown;
    }
}

} // namespace visLib

#endif // _WIN32
