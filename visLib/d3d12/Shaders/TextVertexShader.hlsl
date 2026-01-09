// Text rendering vertex shader
// Input structure from the vertex buffer
struct VSInput
{
    float2 Position : POSITION;  // Screen-space or world-space position
    float2 TexCoord : TEXCOORD;  // UV coordinates in font atlas
};

// Output structure sent to the pixel shader
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

// Constant buffer for transformation matrices (same as mesh rendering)
cbuffer TransformCB : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
};

// Text-specific parameters
cbuffer TextParams : register(b0)
{
    float4 TextColor;
    float2 ScreenSize;
    float2 Padding;
};

// Entry point of the vertex shader
VSOutput main(VSInput input)
{
    VSOutput output;
    
    // For screen-space text rendering:
    // Convert from pixel coordinates to normalized device coordinates
    float2 screenPos = input.Position;
    screenPos.x = (screenPos.x / ScreenSize.x) * 2.0f - 1.0f;  // [0, width] -> [-1, 1]
    screenPos.y = 1.0f - (screenPos.y / ScreenSize.y) * 2.0f;  // [0, height] -> [1, -1]
    
    output.Position = float4(screenPos, 0.0f, 1.0f);
    output.TexCoord = input.TexCoord;
    
    return output;
}
