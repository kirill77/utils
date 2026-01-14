#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Font.h"
#include "utils/visLib/d3d12/internal/D3D12Queue.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include "utils/visLib/d3d12/internal/CD3DX12.h"
#include "utils/visLib/d3d12/internal/D3D12ShaderHelper.h"

// Include stb_truetype
#define STB_TRUETYPE_IMPLEMENTATION
#include "utils/stb/stb_truetype.h"

namespace visLib {

D3D12Font::D3D12Font(uint32_t fontSize, D3D12Queue* pQueue, DXGI_FORMAT rtvFormat)
    : m_fontSize(static_cast<float>(fontSize))
    , m_device(pQueue->getDevice())
    , m_rtvFormat(rtvFormat)
{
    // Atlas dimensions
    const int ATLAS_WIDTH = 1024;
    const int ATLAS_HEIGHT = 1024;
    
    // Load a default system font
    std::string fontPath = "C:/Windows/Fonts/arial.ttf";
    
    // Read font file
    FILE* fontFile = nullptr;
    errno_t err = fopen_s(&fontFile, fontPath.c_str(), "rb");
    if (err != 0 || !fontFile)
    {
        throw std::runtime_error("Failed to open font file: " + fontPath);
    }
    
    // Get file size
    fseek(fontFile, 0, SEEK_END);
    long fileSize = ftell(fontFile);
    fseek(fontFile, 0, SEEK_SET);
    
    // Read font data
    std::vector<unsigned char> fontBuffer(fileSize);
    fread(fontBuffer.data(), 1, fileSize, fontFile);
    fclose(fontFile);
    
    // Initialize font info
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontBuffer.data(), 0))
    {
        throw std::runtime_error("Failed to initialize font");
    }
    
    // Calculate scale for desired font size
    float scale = stbtt_ScaleForPixelHeight(&fontInfo, m_fontSize);
    
    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    m_lineHeight = scale * (ascent - descent + lineGap);
    
    // Create atlas bitmap
    std::vector<unsigned char> atlasData(ATLAS_WIDTH * ATLAS_HEIGHT, 0);
    
    // Character range (printable ASCII)
    const int FIRST_CHAR = 32;  // Space
    const int CHAR_COUNT = 95;  // Up to tilde (~)
    
    // Pack characters into atlas
    stbtt_pack_context packContext;
    stbtt_packedchar packedChars[CHAR_COUNT];
    
    if (!stbtt_PackBegin(&packContext, atlasData.data(), ATLAS_WIDTH, ATLAS_HEIGHT, 0, 1, nullptr))
    {
        throw std::runtime_error("Failed to initialize font packing");
    }
    
    if (!stbtt_PackFontRange(&packContext, fontBuffer.data(), 0, m_fontSize, 
                            FIRST_CHAR, CHAR_COUNT, packedChars))
    {
        stbtt_PackEnd(&packContext);
        throw std::runtime_error("Failed to pack font characters");
    }
    
    stbtt_PackEnd(&packContext);
    
    // Convert to RGBA format for GPU upload
    std::vector<unsigned char> rgbaData(ATLAS_WIDTH * ATLAS_HEIGHT * 4);
    for (int i = 0; i < ATLAS_WIDTH * ATLAS_HEIGHT; ++i)
    {
        rgbaData[i * 4 + 0] = 255;                // Red
        rgbaData[i * 4 + 1] = 255;                // Green  
        rgbaData[i * 4 + 2] = 255;                // Blue
        rgbaData[i * 4 + 3] = atlasData[i];       // Alpha (actual glyph data)
    }
    
    // Store glyph information
    for (int i = 0; i < CHAR_COUNT; ++i)
    {
        char character = static_cast<char>(FIRST_CHAR + i);
        const stbtt_packedchar& packedChar = packedChars[i];
        
        GlyphInfo glyphInfo;
        
        // Texture coordinates (normalized 0-1)
        glyphInfo.texCoordMin = float2(
            static_cast<float>(packedChar.x0) / float(ATLAS_WIDTH), 
            static_cast<float>(packedChar.y0) / float(ATLAS_HEIGHT));
        glyphInfo.texCoordMax = float2(
            static_cast<float>(packedChar.x1) / float(ATLAS_WIDTH), 
            static_cast<float>(packedChar.y1) / float(ATLAS_HEIGHT));
        
        // Glyph dimensions
        glyphInfo.size = float2(
            static_cast<float>(packedChar.x1 - packedChar.x0), 
            static_cast<float>(packedChar.y1 - packedChar.y0));
        
        // Bearing (offset from baseline)
        glyphInfo.bearing = float2(packedChar.xoff, packedChar.yoff);
        
        // Advance to next character
        glyphInfo.advance = packedChar.xadvance;
        
        m_glyphMap[character] = glyphInfo;
    }
    
    // Create the texture resource
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.Width = ATLAS_WIDTH;
    textureDesc.Height = ATLAS_HEIGHT;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_fontTexture)));

    // Upload the atlas data
    const UINT imageSize = ATLAS_WIDTH * ATLAS_HEIGHT * 4;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(imageSize);
    
    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // Get command list from queue
    auto commandList = pQueue->beginRecording();

    // Map the upload buffer and copy atlas data
    UINT8* pData;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
    memcpy(pData, rgbaData.data(), imageSize);
    uploadBuffer->Unmap(0, nullptr);

    // Copy from upload buffer to texture
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Offset = 0;
    srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srcLocation.PlacedFootprint.Footprint.Width = ATLAS_WIDTH;
    srcLocation.PlacedFootprint.Footprint.Height = ATLAS_HEIGHT;
    srcLocation.PlacedFootprint.Footprint.Depth = 1;
    srcLocation.PlacedFootprint.Footprint.RowPitch = ATLAS_WIDTH * 4;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = m_fontTexture.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition resource to shader resource state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_fontTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    
    commandList->ResourceBarrier(1, &barrier);

    // Execute command list
    pQueue->execute(commandList);
}

const GlyphInfo* D3D12Font::getGlyphInfo(char character) const
{
    auto it = m_glyphMap.find(character);
    if (it != m_glyphMap.end())
    {
        return &it->second;
    }
    return nullptr;
}

ID3D12PipelineState* D3D12Font::getTextPSO(ID3D12RootSignature* pRootSignature)
{
    if (!m_textPSO)
    {
        createPSO(pRootSignature);
    }
    return m_textPSO.Get();
}

void D3D12Font::createPSO(ID3D12RootSignature* pRootSignature)
{
    if (m_textPSO)
        return; // Already created
    
    // Load text rendering shaders
    D3D12ShaderHelper& shaderHelper = D3D12ShaderHelper::getInstance();
    
#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif
    
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    
    // Try to load pre-compiled text shaders
    std::wstring shaderPath = L"Shaders/";
    vertexShader = shaderHelper.loadCompiledShader(shaderPath + L"TextVertexShader.cso");
    pixelShader = shaderHelper.loadCompiledShader(shaderPath + L"TextPixelShader.cso");
    
    // Fallback: Compile at runtime
    if (!vertexShader || !pixelShader)
    {
        shaderPath = L"utils/visLib/d3d12/Shaders/";
        if (!vertexShader)
            vertexShader = shaderHelper.loadShader(shaderPath + L"TextVertexShader.hlsl", "main", "vs_5_0", compileFlags);
        if (!pixelShader)
            pixelShader = shaderHelper.loadShader(shaderPath + L"TextPixelShader.hlsl", "main", "ps_5_0", compileFlags);
    }
    
    if (!vertexShader || !pixelShader)
    {
        throw std::runtime_error("Failed to load text rendering shaders");
    }
    
    // Define vertex input layout for text rendering
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    
    // Setup graphics pipeline state for text rendering
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    
    // Root signature
    psoDesc.pRootSignature = pRootSignature;
    
    // Shaders
    psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
    psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();
    
    // Rasterizer state (optimized for text rendering)
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = FALSE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    
    // Blend state (enable alpha blending for text)
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;
    
    // Depth stencil state (disabled for text)
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencilDesc.StencilEnable = FALSE;
    psoDesc.DepthStencilState = depthStencilDesc;
    
    // Input layout
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    
    // Primitive topology
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    
    // Render target format
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_rtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    
    // MSAA
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;
    
    // Misc
    psoDesc.NodeMask = 0;
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    
    // Create the PSO
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_textPSO)));
}

} // namespace visLib

#endif // _WIN32
