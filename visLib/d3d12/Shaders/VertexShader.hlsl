// Basic vertex shader for rendering 3D meshes
// Input structure from the vertex buffer
struct VSInput
{
    float3 Position : POSITION;
};

// Output structure sent to the pixel shader
struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR;
};

// Constant buffer for view and projection matrices
cbuffer TransformCB : register(b0)
{
    matrix View;
    matrix Projection;
};

// Root constants for world matrix (per-object)
cbuffer WorldMatrix : register(b2)
{
    matrix World;
};

// Entry point of the vertex shader
VSOutput main(VSInput input)
{
    VSOutput output;
    
    // Transform the vertex position through world, view, and projection matrices
    float4 worldPosition = mul(World, float4(input.Position, 1.0f));
    float4 viewPosition = mul(View, worldPosition);
    output.Position = mul(Projection, viewPosition);
    
    // Generate a simple color based on position
    output.Color = normalize(input.Position) * 0.5f + 0.5f;
    
    return output;
}
