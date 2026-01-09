#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IText.h"
#include "D3D12Font.h"
#include <vector>
#include <ctime>

namespace visLib {
namespace d3d12 {

// Forward declarations
class SwapChain;

// D3D12TextLine - D3D12 implementation of TextLine
class D3D12TextLine : public TextLine
{
public:
    D3D12TextLine();
    ~D3D12TextLine() override = default;

    // TextLine interface implementation
    int printf(const char* format, ...) override;
    const std::string& getText() const override { return m_text; }
    bool isEmpty() const override { return m_text.empty(); }
    void setColor(const float4& color) override;
    const float4& getColor() const override { return m_color; }
    void setLifetime(uint32_t seconds) override { m_lifetimeSec = seconds; }
    uint32_t getLifetime() const override { return m_lifetimeSec; }

    // Internal methods
    std::time_t getCreateTime() const { return m_createTime; }

private:
    std::string m_text;
    float4 m_color;
    uint32_t m_lifetimeSec = 0;
    std::time_t m_createTime;
};

// D3D12Text - D3D12 implementation of IText
class D3D12Text : public IText
{
public:
    D3D12Text(std::shared_ptr<D3D12Font> pFont);
    ~D3D12Text() override = default;

    // IText interface implementation
    void setPosition(const float2& position) override { m_position = position; }
    std::shared_ptr<TextLine> createLine() override;
    void setDefaultColor(const float4& color) override { m_defaultColor = color; }
    std::shared_ptr<IFont> getFont() const override { return m_pFont; }

    // D3D12-specific methods
    void render(SwapChain* pSwapChain, 
                ID3D12RootSignature* pRootSignature,
                ID3D12GraphicsCommandList* pCmdList);

private:
    // Text vertex structure (matches shader input)
    struct TextVertex
    {
        float2 position;    // Pixel coordinates
        float2 texCoord;    // UV coordinates
    };
    
    // Text parameters constant buffer
    struct TextParams
    {
        float4 textColor;
        float2 screenSize;
        float2 padding;
    };
    
    bool isExpired(const std::shared_ptr<D3D12TextLine>& line) const;
    void generateTextQuads(std::vector<TextVertex>& vertices, 
                           std::vector<uint16_t>& indices,
                           float2 screenSize);
    void updateVertexBuffer(const std::vector<TextVertex>& vertices,
                           const std::vector<uint16_t>& indices,
                           ID3D12Device* pDevice);
    void updateConstantBuffer(const float2& screenSize, ID3D12Device* pDevice);
    void ensureDescriptorHeaps(ID3D12Device* pDevice);

private:
    std::shared_ptr<D3D12Font> m_pFont;
    float2 m_position;
    float4 m_defaultColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    std::vector<std::shared_ptr<D3D12TextLine>> m_lines;
    
    // Rendering resources
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
    
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    uint8_t* m_constantBufferData = nullptr;
};

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
