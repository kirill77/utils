#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/InputState.h"
#include <array>

namespace visLib {

// D3D12InputState - Win32 implementation of InputState interface
class D3D12InputState : public InputState
{
public:
    D3D12InputState();
    ~D3D12InputState() override = default;

    // InputState interface implementation
    bool isKeyDown(Key key) const override;
    bool isKeyPressed(Key key) const override;
    bool isKeyReleased(Key key) const override;
    float2 getMousePosition() const override;
    float2 getMouseDelta() const override;
    float getScrollDelta() const override;

    // Internal methods called by D3D12Window
    void beginFrame();
    void endFrame();
    
    // Process Win32 messages
    void onKeyDown(WPARAM vkCode);
    void onKeyUp(WPARAM vkCode);
    void onMouseMove(int x, int y);
    void onMouseButton(Key button, bool down);
    void onMouseWheel(float delta);

private:
    // Convert Win32 virtual key code to visLib::Key
    static Key vkToKey(WPARAM vkCode);
    
    // Key state arrays
    std::array<bool, static_cast<size_t>(Key::Count)> m_currentState;
    std::array<bool, static_cast<size_t>(Key::Count)> m_previousState;
    
    // Mouse state
    float2 m_mousePosition;
    float2 m_mouseDelta;
    float2 m_lastMousePosition;
    float m_scrollDelta;
    
    bool m_firstFrame;
};

} // namespace visLib

#endif // _WIN32
