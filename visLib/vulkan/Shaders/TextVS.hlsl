// TextVS.hlsl - Vulkan port of d3d12/Shaders/TextVertexShader.hlsl
// Screen-space text rendering: pixel coordinates -> NDC.

struct VSInput {
    float2 Position : POSITION;   // Pixel coordinates.
    float2 TexCoord : TEXCOORD;   // UV in font atlas.
};

struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD;
};

[[vk::binding(0, 0)]]
cbuffer TextParams {
    float4 TextColor;
    float2 ScreenSize;
    float2 _padding;
};

VSOutput main(VSInput input) {
    VSOutput output;

    float2 screenPos = input.Position;
    // Vulkan NDC has +Y pointing down: pixel Y=0 (top) -> NDC Y=-1,
    // pixel Y=h (bottom) -> NDC Y=+1. (The D3D12 shader does the opposite
    // because D3D12 NDC has +Y up.)
    screenPos.x = (screenPos.x / ScreenSize.x) * 2.0f - 1.0f;   // [0, w] -> [-1,  1]
    screenPos.y = (screenPos.y / ScreenSize.y) * 2.0f - 1.0f;   // [0, h] -> [-1,  1]

    output.Position = float4(screenPos, 0.0f, 1.0f);
    output.TexCoord = input.TexCoord;
    return output;
}
