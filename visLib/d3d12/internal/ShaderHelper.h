#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include <unordered_map>
#include <string>

namespace visLib {
namespace d3d12 {

// ShaderHelper - Singleton for shader loading and caching
class ShaderHelper
{
public:
    static ShaderHelper& getInstance();

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
    ShaderHelper() = default;
    ~ShaderHelper() = default;
    ShaderHelper(const ShaderHelper&) = delete;
    ShaderHelper& operator=(const ShaderHelper&) = delete;

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3DBlob>> m_shaderCache;
};

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
