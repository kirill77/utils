#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IFont.h"
#include <unordered_map>

namespace visLib {

// Forward declarations
class D3D12Queue;

// D3D12Font - D3D12 implementation of IFont interface
class D3D12Font : public IFont, public std::enable_shared_from_this<D3D12Font>
{
public:
    D3D12Font(uint32_t fontSize, D3D12Queue* pQueue, DXGI_FORMAT rtvFormat);
    ~D3D12Font() override = default;

    // IFont interface implementation
    float getFontSize() const override { return m_fontSize; }
    float getLineHeight() const override { return m_lineHeight; }
    const GlyphInfo* getGlyphInfo(char character) const override;

    // D3D12-specific accessors
    ID3D12Resource* getFontTexture() const { return m_fontTexture.Get(); }
    ID3D12PipelineState* getTextPSO(ID3D12RootSignature* pRootSignature);

private:
    void createPSO(ID3D12RootSignature* pRootSignature);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_fontTexture;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_textPSO;
    
    std::unordered_map<char, GlyphInfo> m_glyphMap;
    float m_fontSize;
    float m_lineHeight;
    DXGI_FORMAT m_rtvFormat;
};

} // namespace visLib

#endif // _WIN32
