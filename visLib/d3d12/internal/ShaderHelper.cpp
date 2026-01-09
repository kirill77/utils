#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/d3d12/internal/ShaderHelper.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include <fstream>
#include <vector>
#include <d3dcompiler.h>
#include <filesystem>

namespace visLib {
namespace d3d12 {

ShaderHelper& ShaderHelper::getInstance()
{
    static ShaderHelper instance;
    return instance;
}

// Helper to find shader file in common locations
static bool findShaderFile(const std::wstring& filename, std::filesystem::path& foundPath)
{
    // Check if file exists directly
    std::filesystem::path directPath(filename);
    if (std::filesystem::exists(directPath))
    {
        foundPath = directPath;
        return true;
    }

    // Check common shader directories
    std::vector<std::filesystem::path> searchPaths = {
        std::filesystem::current_path(),
        std::filesystem::current_path() / "Shaders",
        std::filesystem::current_path() / "shaders",
        std::filesystem::current_path() / ".." / "Shaders",
        std::filesystem::current_path() / ".." / "shaders"
    };

    for (const auto& searchPath : searchPaths)
    {
        std::filesystem::path testPath = searchPath / filename;
        if (std::filesystem::exists(testPath))
        {
            foundPath = testPath;
            return true;
        }
    }

    return false;
}

Microsoft::WRL::ComPtr<ID3DBlob> ShaderHelper::loadShader(
    const std::wstring& filePath,
    const std::string& entryPoint,
    const std::string& target,
    UINT compileFlags)
{
    // Create a unique key for the shader
    std::wstring shaderKey = filePath + L":" + std::wstring(entryPoint.begin(), entryPoint.end()) + L":" + std::wstring(target.begin(), target.end());
    
    // Check if already loaded
    auto it = m_shaderCache.find(shaderKey);
    if (it != m_shaderCache.end())
    {
        return it->second;
    }
    
    // Find the shader file
    std::filesystem::path foundPath;
    if (!findShaderFile(filePath, foundPath))
    {
        std::filesystem::path pathObj(filePath);
        throw std::runtime_error("Failed to find shader file: " + pathObj.string());
    }
    
    // Compile shader
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    
    HRESULT hr = D3DCompileFromFile(
        foundPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        target.c_str(),
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);
    
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(hr);
    }
    
    // Cache the shader
    m_shaderCache[shaderKey] = shaderBlob;
    
    return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3DBlob> ShaderHelper::loadCompiledShader(const std::wstring& filePath)
{
    // Check if already loaded
    auto it = m_shaderCache.find(filePath);
    if (it != m_shaderCache.end())
    {
        return it->second;
    }
    
    // Find the shader file
    std::filesystem::path foundPath;
    if (!findShaderFile(filePath, foundPath))
    {
        return nullptr;
    }
    
    // Load the shader
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    if (FAILED(D3DReadFileToBlob(foundPath.c_str(), &shaderBlob)))
    {
        return nullptr;
    }
    
    // Cache the shader
    m_shaderCache[filePath] = shaderBlob;
    
    return shaderBlob;
}

void ShaderHelper::clearCache()
{
    m_shaderCache.clear();
}

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
