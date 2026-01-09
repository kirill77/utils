#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IMesh.h"

namespace visLib {
namespace d3d12 {

// D3D12Mesh - D3D12 implementation of IMesh interface
class D3D12Mesh : public IMesh
{
public:
    D3D12Mesh(Microsoft::WRL::ComPtr<ID3D12Device> device);
    ~D3D12Mesh() override = default;

    // IMesh interface implementation
    void setGeometry(
        const std::vector<Vertex>& vertices,
        const std::vector<int3>& triangles) override;
    
    uint32_t getVertexCount() const override { return m_vertexCount; }
    uint32_t getTriangleCount() const override { return m_indexCount / 3; }
    uint32_t getIndexCount() const override { return m_indexCount; }
    const box3& getBoundingBox() const override { return m_boundingBox; }
    bool isEmpty() const override { return m_indexCount == 0; }

    // D3D12-specific accessors (used by D3D12Renderer)
    D3D12_VERTEX_BUFFER_VIEW getVertexBufferView() const { return m_vertexBufferView; }
    D3D12_INDEX_BUFFER_VIEW getIndexBufferView() const { return m_indexBufferView; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> createOrUpdateUploadBuffer(
        const void* data,
        uint64_t byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& existingBuffer);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    box3 m_boundingBox;
};

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
