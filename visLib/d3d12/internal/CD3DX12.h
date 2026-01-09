//=============================================================================
// CD3DX12 Helper Structures
// Inline helper structures for common D3D12 operations
// Based on Microsoft's d3dx12.h from DirectX-Headers
//=============================================================================

#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"

//=============================================================================
// CD3DX12_HEAP_PROPERTIES
//=============================================================================
struct CD3DX12_HEAP_PROPERTIES : public D3D12_HEAP_PROPERTIES
{
    CD3DX12_HEAP_PROPERTIES() = default;
    explicit CD3DX12_HEAP_PROPERTIES(const D3D12_HEAP_PROPERTIES& o) noexcept : D3D12_HEAP_PROPERTIES(o) {}
    
    CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE type, UINT creationNodeMask = 1, UINT nodeMask = 1) noexcept
    {
        Type = type;
        CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        CreationNodeMask = creationNodeMask;
        VisibleNodeMask = nodeMask;
    }
};

//=============================================================================
// CD3DX12_RESOURCE_DESC
//=============================================================================
struct CD3DX12_RESOURCE_DESC : public D3D12_RESOURCE_DESC
{
    CD3DX12_RESOURCE_DESC() = default;
    explicit CD3DX12_RESOURCE_DESC(const D3D12_RESOURCE_DESC& o) noexcept : D3D12_RESOURCE_DESC(o) {}
    
    static inline CD3DX12_RESOURCE_DESC Buffer(UINT64 width, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, UINT64 alignment = 0) noexcept
    {
        CD3DX12_RESOURCE_DESC desc;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = alignment;
        desc.Width = width;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = flags;
        return desc;
    }
};

//=============================================================================
// CD3DX12_RANGE
//=============================================================================
struct CD3DX12_RANGE : public D3D12_RANGE
{
    CD3DX12_RANGE() = default;
    explicit CD3DX12_RANGE(const D3D12_RANGE& o) noexcept : D3D12_RANGE(o) {}
    CD3DX12_RANGE(SIZE_T begin, SIZE_T end) noexcept
    {
        Begin = begin;
        End = end;
    }
};

//=============================================================================
// CD3DX12_RESOURCE_BARRIER
//=============================================================================
struct CD3DX12_RESOURCE_BARRIER : public D3D12_RESOURCE_BARRIER
{
    CD3DX12_RESOURCE_BARRIER() noexcept
    {
        ZeroMemory(this, sizeof(*this));
    }
    explicit CD3DX12_RESOURCE_BARRIER(const D3D12_RESOURCE_BARRIER& o) noexcept : D3D12_RESOURCE_BARRIER(o) {}
    
    static inline CD3DX12_RESOURCE_BARRIER Transition(
        ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE) noexcept
    {
        CD3DX12_RESOURCE_BARRIER result = {};
        result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        result.Flags = flags;
        // Cast to base type to access the Transition member (avoid name collision with static method)
        D3D12_RESOURCE_BARRIER& base = result;
        base.Transition.pResource = pResource;
        base.Transition.StateBefore = stateBefore;
        base.Transition.StateAfter = stateAfter;
        base.Transition.Subresource = subresource;
        return result;
    }
};

//=============================================================================
// CD3DX12_CPU_DESCRIPTOR_HANDLE
//=============================================================================
struct CD3DX12_CPU_DESCRIPTOR_HANDLE : public D3D12_CPU_DESCRIPTOR_HANDLE
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE() = default;
    explicit CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE& o) noexcept : D3D12_CPU_DESCRIPTOR_HANDLE(o) {}
    CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_CPU_DESCRIPTOR_HANDLE other, INT offsetScaledByIncrementSize) noexcept
    {
        InitOffsetted(other, offsetScaledByIncrementSize);
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_CPU_DESCRIPTOR_HANDLE other, INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
    {
        InitOffsetted(other, offsetInDescriptors, descriptorIncrementSize);
    }
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetScaledByIncrementSize) noexcept
    {
        ptr = SIZE_T(INT64(ptr) + INT64(offsetScaledByIncrementSize));
        return *this;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
    {
        ptr = SIZE_T(INT64(ptr) + INT64(offsetInDescriptors) * INT64(descriptorIncrementSize));
        return *this;
    }
    
    void InitOffsetted(D3D12_CPU_DESCRIPTOR_HANDLE base, INT offsetScaledByIncrementSize) noexcept
    {
        ptr = SIZE_T(INT64(base.ptr) + INT64(offsetScaledByIncrementSize));
    }
    void InitOffsetted(D3D12_CPU_DESCRIPTOR_HANDLE base, INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
    {
        ptr = SIZE_T(INT64(base.ptr) + INT64(offsetInDescriptors) * INT64(descriptorIncrementSize));
    }
};

//=============================================================================
// CD3DX12_GPU_DESCRIPTOR_HANDLE
//=============================================================================
struct CD3DX12_GPU_DESCRIPTOR_HANDLE : public D3D12_GPU_DESCRIPTOR_HANDLE
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE() = default;
    explicit CD3DX12_GPU_DESCRIPTOR_HANDLE(const D3D12_GPU_DESCRIPTOR_HANDLE& o) noexcept : D3D12_GPU_DESCRIPTOR_HANDLE(o) {}
    CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_GPU_DESCRIPTOR_HANDLE other, INT offsetScaledByIncrementSize) noexcept
    {
        InitOffsetted(other, offsetScaledByIncrementSize);
    }
    CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_GPU_DESCRIPTOR_HANDLE other, INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
    {
        InitOffsetted(other, offsetInDescriptors, descriptorIncrementSize);
    }
    
    void InitOffsetted(D3D12_GPU_DESCRIPTOR_HANDLE base, INT offsetScaledByIncrementSize) noexcept
    {
        ptr = UINT64(INT64(base.ptr) + INT64(offsetScaledByIncrementSize));
    }
    void InitOffsetted(D3D12_GPU_DESCRIPTOR_HANDLE base, INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
    {
        ptr = UINT64(INT64(base.ptr) + INT64(offsetInDescriptors) * INT64(descriptorIncrementSize));
    }
};

//=============================================================================
// CD3DX12_ROOT_PARAMETER
//=============================================================================
struct CD3DX12_ROOT_PARAMETER : public D3D12_ROOT_PARAMETER
{
    CD3DX12_ROOT_PARAMETER() = default;
    explicit CD3DX12_ROOT_PARAMETER(const D3D12_ROOT_PARAMETER& o) noexcept : D3D12_ROOT_PARAMETER(o) {}
    
    inline void InitAsDescriptorTable(
        UINT numDescriptorRanges,
        const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) noexcept
    {
        ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ShaderVisibility = visibility;
        DescriptorTable.NumDescriptorRanges = numDescriptorRanges;
        DescriptorTable.pDescriptorRanges = pDescriptorRanges;
    }
    
    inline void InitAsConstants(
        UINT num32BitValues,
        UINT shaderRegister,
        UINT registerSpace = 0,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) noexcept
    {
        ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ShaderVisibility = visibility;
        Constants.Num32BitValues = num32BitValues;
        Constants.ShaderRegister = shaderRegister;
        Constants.RegisterSpace = registerSpace;
    }
};

//=============================================================================
// CD3DX12_DESCRIPTOR_RANGE
//=============================================================================
struct CD3DX12_DESCRIPTOR_RANGE : public D3D12_DESCRIPTOR_RANGE
{
    CD3DX12_DESCRIPTOR_RANGE() = default;
    explicit CD3DX12_DESCRIPTOR_RANGE(const D3D12_DESCRIPTOR_RANGE& o) noexcept : D3D12_DESCRIPTOR_RANGE(o) {}
    
    inline void Init(
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
        UINT numDescriptors,
        UINT baseShaderRegister,
        UINT registerSpace = 0,
        UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) noexcept
    {
        RangeType = rangeType;
        NumDescriptors = numDescriptors;
        BaseShaderRegister = baseShaderRegister;
        RegisterSpace = registerSpace;
        OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;
    }
};

//=============================================================================
// CD3DX12_ROOT_SIGNATURE_DESC
//=============================================================================
struct CD3DX12_ROOT_SIGNATURE_DESC : public D3D12_ROOT_SIGNATURE_DESC
{
    CD3DX12_ROOT_SIGNATURE_DESC() = default;
    explicit CD3DX12_ROOT_SIGNATURE_DESC(const D3D12_ROOT_SIGNATURE_DESC& o) noexcept : D3D12_ROOT_SIGNATURE_DESC(o) {}
    
    inline void Init(
        UINT numParameters,
        const D3D12_ROOT_PARAMETER* pParameters,
        UINT numStaticSamplers = 0,
        const D3D12_STATIC_SAMPLER_DESC* pStaticSamplers = nullptr,
        D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE) noexcept
    {
        NumParameters = numParameters;
        pParameters = pParameters;
        NumStaticSamplers = numStaticSamplers;
        pStaticSamplers = pStaticSamplers;
        Flags = flags;
        this->pParameters = pParameters;
    }
};

#endif // _WIN32
