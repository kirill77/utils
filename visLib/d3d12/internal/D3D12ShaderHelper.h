#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include <unordered_map>
#include <string>

namespace visLib {

// D3D12ShaderHelper - Singleton for shader loading and caching
class D3D12ShaderHelper
{
public:
    static D3D12ShaderHelper& getInstance();

    // Load and compile a shader from source file
    Microsoft::WRL::ComPtr<ID3DBlob> loadShader(
        const std::wstring& filePath,
        const std::string& entryPoint,
        const std::string& target,
        UINT compileFlags = 0);

    // Load a pre-compiled shader (.cso file)
    Microsoft::WRL::ComPtr<ID3DBlob> loadCompiledShader(const std::wstring& filePath);

    // Clear shader cache
    void clearCache();

private:
    D3D12ShaderHelper() = default;
    ~D3D12ShaderHelper() = default;
    D3D12ShaderHelper(const D3D12ShaderHelper&) = delete;
    D3D12ShaderHelper& operator=(const D3D12ShaderHelper&) = delete;

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3DBlob>> m_shaderCache;
};

} // namespace visLib

#endif // _WIN32
