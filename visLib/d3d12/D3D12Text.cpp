#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Text.h"
#include "utils/visLib/d3d12/internal/D3D12SwapChain.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include "utils/visLib/d3d12/internal/CD3DX12.h"
#include <cstdarg>
#include <algorithm>

namespace visLib {

//=============================================================================
// D3D12TextLine implementation
//=============================================================================

D3D12TextLine::D3D12TextLine()
    : m_color(1.0f, 1.0f, 1.0f, 1.0f)
    , m_createTime(std::time(nullptr))
{
}

int D3D12TextLine::printf(const char* format, ...)
{
    if (!format)
    {
        return 0;
    }
    
    va_list args;
    va_start(args, format);
    
    // Determine required buffer size
    va_list args_copy;
    va_copy(args_copy, args);
    int required_size = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    
    if (required_size < 0)
    {
        va_end(args);
        return 0;
    }
    
    // Allocate buffer and format the string
    std::vector<char> buffer(required_size + 1);
    int formatted_chars = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    
    if (formatted_chars < 0)
    {
        return 0;
    }
    
    m_text = std::string(buffer.data());
    return formatted_chars;
}

void D3D12TextLine::setColor(const float4& color)
{
    m_color.x = std::max(0.0f, std::min(1.0f, color.x));
    m_color.y = std::max(0.0f, std::min(1.0f, color.y));
    m_color.z = std::max(0.0f, std::min(1.0f, color.z));
    m_color.w = std::max(0.0f, std::min(1.0f, color.w));
}

//=============================================================================
// D3D12Text implementation
//=============================================================================

D3D12Text::D3D12Text(std::shared_ptr<D3D12Font> pFont)
    : m_pFont(pFont)
    , m_position(0.0f, 0.0f)
{
}

std::shared_ptr<TextLine> D3D12Text::createLine()
{
    auto newLine = std::make_shared<D3D12TextLine>();
    newLine->setColor(m_defaultColor);
    m_lines.push_back(newLine);
    return newLine;
}

bool D3D12Text::isExpired(const std::shared_ptr<D3D12TextLine>& line) const
{
    uint32_t lifetime = line->getLifetime();
    
    // If nobody holds reference and no explicit lifetime, auto-expire after 5 seconds
    if (line.use_count() == 1 && lifetime == 0)
    {
        lifetime = 5;
    }
    
    if (lifetime == 0)
    {
        return false; // Lives forever
    }
    
    std::time_t currentTime = std::time(nullptr);
    return (currentTime - line->getCreateTime()) >= static_cast<std::time_t>(lifetime);
}

void D3D12Text::generateTextQuads(
    std::vector<TextVertex>& vertices, 
    std::vector<uint16_t>& indices,
    float2 screenSize)
{
    vertices.clear();
    indices.clear();
    
    // Remove expired lines
    m_lines.erase(
        std::remove_if(m_lines.begin(), m_lines.end(),
            [this](const std::shared_ptr<D3D12TextLine>& line) {
                return isExpired(line);
            }),
        m_lines.end());
    
    if (m_lines.empty() || !m_pFont)
    {
        return;
    }
    
    float currentX = m_position.x;
    float currentY = m_position.y;
    float lineHeight = m_pFont->getLineHeight();
    
    uint16_t vertexIndex = 0;
    
    for (const auto& pLine : m_lines)
    {
        if (!pLine || pLine->isEmpty())
        {
            continue;
        }
        
        currentX = m_position.x;
        
        for (char c : pLine->getText())
        {
            const GlyphInfo* glyphInfo = m_pFont->getGlyphInfo(c);
            if (!glyphInfo)
            {
                continue;
            }
            
            // Calculate glyph position
            float glyphX = currentX + glyphInfo->bearing.x;
            float glyphY = currentY + lineHeight + glyphInfo->bearing.y;
            float glyphWidth = glyphInfo->size.x;
            float glyphHeight = glyphInfo->size.y;
            
            // Create quad vertices
            TextVertex quad[4];
            
            // Top-left
            quad[0].position = float2(glyphX, glyphY);
            quad[0].texCoord = glyphInfo->texCoordMin;
            
            // Top-right
            quad[1].position = float2(glyphX + glyphWidth, glyphY);
            quad[1].texCoord = float2(glyphInfo->texCoordMax.x, glyphInfo->texCoordMin.y);
            
            // Bottom-right
            quad[2].position = float2(glyphX + glyphWidth, glyphY + glyphHeight);
            quad[2].texCoord = glyphInfo->texCoordMax;
            
            // Bottom-left
            quad[3].position = float2(glyphX, glyphY + glyphHeight);
            quad[3].texCoord = float2(glyphInfo->texCoordMin.x, glyphInfo->texCoordMax.y);
            
            // Add vertices
            for (int i = 0; i < 4; i++)
            {
                vertices.push_back(quad[i]);
            }
            
            // Add indices for two triangles
            indices.push_back(vertexIndex + 0);
            indices.push_back(vertexIndex + 1);
            indices.push_back(vertexIndex + 2);
            
            indices.push_back(vertexIndex + 0);
            indices.push_back(vertexIndex + 2);
            indices.push_back(vertexIndex + 3);
            
            vertexIndex += 4;
            
            // Advance to next character
            currentX += glyphInfo->advance;
        }
        
        // Move to next line
        currentY += lineHeight;
    }
}

void D3D12Text::updateVertexBuffer(
    const std::vector<TextVertex>& vertices,
    const std::vector<uint16_t>& indices,
    ID3D12Device* pDevice)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }
    
    UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(TextVertex));
    UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    
    // Create or recreate vertex buffer if size changed
    if (!m_vertexBuffer || m_vertexCount != vertices.size())
    {
        m_vertexBuffer = CreateBuffer(pDevice, vertexBufferSize,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
        
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(TextVertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
        m_vertexCount = static_cast<uint32_t>(vertices.size());
    }
    
    // Create or recreate index buffer if size changed
    if (!m_indexBuffer || m_indexCount != indices.size())
    {
        m_indexBuffer = CreateBuffer(pDevice, indexBufferSize,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_GENERIC_READ);
        
        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_indexBufferView.SizeInBytes = indexBufferSize;
        m_indexCount = static_cast<uint32_t>(indices.size());
    }
    
    // Upload vertex data
    UploadToBuffer(m_vertexBuffer.Get(), vertices.data(), static_cast<uint32_t>(vertices.size()));
    
    // Upload index data
    UploadToBuffer(m_indexBuffer.Get(), indices.data(), static_cast<uint32_t>(indices.size()));
}

void D3D12Text::updateConstantBuffer(const float2& screenSize, ID3D12Device* pDevice)
{
    // Create constant buffer if it doesn't exist
    if (!m_constantBuffer)
    {
        const UINT constantBufferSize = (sizeof(TextParams) + 255) & ~255;
        
        m_constantBuffer = CreateBuffer(pDevice, constantBufferSize,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_GENERIC_READ);
        
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_constantBufferData)));
    }
    
    // Update constant buffer data
    if (m_constantBufferData)
    {
        TextParams params;
        params.textColor = m_defaultColor;
        params.screenSize = screenSize;
        params.padding = float2(0.0f, 0.0f);
        
        memcpy(m_constantBufferData, &params, sizeof(TextParams));
    }
}

void D3D12Text::ensureDescriptorHeaps(ID3D12Device* pDevice)
{
    if (!m_descriptorHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2; // CBV + SRV
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));
        
        UINT descriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE heapStart(m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());
        
        // Create CBV for text parameters at index 0
        if (m_constantBuffer)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = (sizeof(TextParams) + 255) & ~255;
            pDevice->CreateConstantBufferView(&cbvDesc, heapStart);
        }
        
        // Create SRV for font atlas at index 1
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        
        ID3D12Resource* fontTexture = m_pFont->getFontTexture();
        if (fontTexture)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(heapStart, 1, descriptorSize);
            pDevice->CreateShaderResourceView(fontTexture, &srvDesc, srvHandle);
        }
    }
}

void D3D12Text::render(
    const D3D12RenderTarget& target,
    ID3D12RootSignature* pRootSignature,
    ID3D12GraphicsCommandList* pCmdList)
{
    if (m_lines.empty() || !m_pFont || !pRootSignature || !pCmdList)
    {
        return;
    }

    if (target.width == 0 || target.height == 0)
    {
        return;
    }

    float2 screenSize = float2(static_cast<float>(target.width),
                               static_cast<float>(target.height));

    // Get device from command list
    Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
    pCmdList->GetDevice(IID_PPV_ARGS(&pDevice));

    // Generate text geometry
    std::vector<TextVertex> vertices;
    std::vector<uint16_t> indices;
    generateTextQuads(vertices, indices, screenSize);

    if (vertices.empty() || indices.empty())
    {
        return;
    }

    // Update buffers
    updateVertexBuffer(vertices, indices, pDevice.Get());
    updateConstantBuffer(screenSize, pDevice.Get());

    // Ensure descriptor heaps exist
    ensureDescriptorHeaps(pDevice.Get());

    // Update CBV
    if (m_constantBuffer && m_descriptorHeap)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE heapStart(m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (sizeof(TextParams) + 255) & ~255;
        pDevice->CreateConstantBufferView(&cbvDesc, heapStart);
    }

    // Transition to render target state if resource provided (caller wants us to manage barriers)
    if (target.pResource)
    {
        CD3DX12_RESOURCE_BARRIER renderTargetBarrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                target.pResource.Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        pCmdList->ResourceBarrier(1, &renderTargetBarrier);
    }

    // Set render targets
    pCmdList->OMSetRenderTargets(1, &target.rtvHandle, FALSE, &target.dsvHandle);

    // Set viewport and scissor rect
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = screenSize.x;
    viewport.Height = screenSize.y;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    pCmdList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(screenSize.x);
    scissorRect.bottom = static_cast<LONG>(screenSize.y);
    pCmdList->RSSetScissorRects(1, &scissorRect);

    // Get text PSO
    auto textPSO = m_pFont->getTextPSO(pRootSignature);
    if (!textPSO)
    {
        return;
    }

    // Set pipeline state
    pCmdList->SetGraphicsRootSignature(pRootSignature);
    pCmdList->SetPipelineState(textPSO);

    // Set primitive topology
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex and index buffers
    pCmdList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    pCmdList->IASetIndexBuffer(&m_indexBufferView);

    // Set descriptor heaps
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap.Get() };
    pCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    // Set root signature parameters
    UINT descriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_GPU_DESCRIPTOR_HANDLE heapStart(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart());

    pCmdList->SetGraphicsRootDescriptorTable(0, heapStart);
    pCmdList->SetGraphicsRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(heapStart, 1, descriptorSize));

    // Draw text
    pCmdList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);

    // Transition back to present state if we managed the barrier
    if (target.pResource)
    {
        CD3DX12_RESOURCE_BARRIER presentBarrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                target.pResource.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT);
        pCmdList->ResourceBarrier(1, &presentBarrier);
    }
}

void D3D12Text::render(
    D3D12SwapChain* pSwapChain,
    ID3D12RootSignature* pRootSignature,
    ID3D12GraphicsCommandList* pCmdList)
{
    if (!pSwapChain)
    {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    ThrowIfFailed(pSwapChain->getSwapChain()->GetDesc1(&swapChainDesc));

    D3D12RenderTarget target;
    target.width = swapChainDesc.Width;
    target.height = swapChainDesc.Height;
    target.rtvHandle = pSwapChain->getBBColorCPUHandle();
    target.dsvHandle = pSwapChain->getBBDepthCPUHandle();
    target.pResource = pSwapChain->getBBColor();

    render(target, pRootSignature, pCmdList);
}

} // namespace visLib

#endif // _WIN32
