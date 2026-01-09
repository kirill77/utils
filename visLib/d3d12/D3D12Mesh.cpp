#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/d3d12/D3D12Mesh.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include "utils/visLib/d3d12/internal/CD3DX12.h"
#include <stdexcept>

namespace visLib {
namespace d3d12 {

D3D12Mesh::D3D12Mesh(Microsoft::WRL::ComPtr<ID3D12Device> device)
    : m_device(device)
{
}

Microsoft::WRL::ComPtr<ID3D12Resource> D3D12Mesh::createOrUpdateUploadBuffer(
    const void* data,
    uint64_t byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& existingBuffer)
{
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    // If buffer already exists with same size, reuse it
    if (existingBuffer)
    {
        D3D12_RESOURCE_DESC existingDesc = existingBuffer->GetDesc();
        if (existingDesc.Width == byteSize)
        {
            uploadBuffer = existingBuffer;
        }
        else
        {
            // Size changed, need to create new buffer
            existingBuffer.Reset();
        }
    }
    
    if (!uploadBuffer)
    {
        // Create buffer in upload heap (CPU-visible)
        CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        
        ThrowIfFailed(m_device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadBuffer.GetAddressOf())));
    }

    // Map and copy data directly
    void* mappedData = nullptr;
    ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mappedData));
    memcpy(mappedData, data, static_cast<size_t>(byteSize));
    uploadBuffer->Unmap(0, nullptr);

    return uploadBuffer;
}

void D3D12Mesh::setGeometry(
    const std::vector<Vertex>& vertices,
    const std::vector<int3>& triangles)
{
    // Compute bounding box from vertices
    if (!vertices.empty())
    {
        float3 minPoint = vertices[0].position;
        float3 maxPoint = vertices[0].position;
        
        for (size_t i = 1; i < vertices.size(); ++i)
        {
            const float3& pos = vertices[i].position;
            minPoint = min(minPoint, pos);
            maxPoint = max(maxPoint, pos);
        }
        
        m_boundingBox = box3(minPoint, maxPoint);
    }
    else
    {
        m_boundingBox = box3::empty();
    }
    
    m_vertexCount = static_cast<uint32_t>(vertices.size());
    
    // Create vertex buffer using CPU-visible upload heap
    const UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    if (vbSize > 0)
    {
        m_vertexBuffer = createOrUpdateUploadBuffer(
            vertices.data(),
            vbSize,
            m_vertexBuffer);
        
        // Create vertex buffer view
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vbSize;
    }
    else
    {
        m_vertexBuffer.Reset();
        m_vertexBufferView = {};
    }
    
    // Convert int3 indices to UINT array
    std::vector<UINT> indices;
    indices.reserve(triangles.size() * 3);
    for (const auto& tri : triangles)
    {
        indices.push_back(static_cast<UINT>(tri.x));
        indices.push_back(static_cast<UINT>(tri.y));
        indices.push_back(static_cast<UINT>(tri.z));
    }
    
    m_indexCount = static_cast<uint32_t>(indices.size());
    
    // Create index buffer using CPU-visible upload heap
    const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(UINT));
    if (ibSize > 0)
    {
        m_indexBuffer = createOrUpdateUploadBuffer(
            indices.data(),
            ibSize,
            m_indexBuffer);
        
        // Create index buffer view
        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        m_indexBufferView.SizeInBytes = ibSize;
    }
    else
    {
        m_indexBuffer.Reset();
        m_indexBufferView = {};
    }
}

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
