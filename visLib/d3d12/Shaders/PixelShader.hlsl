// Basic pixel shader for rendering 3D meshes
// Input structure from the vertex shader
struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR;
};

// Output structure
struct PSOutput
{
    float4 Color : SV_TARGET;
};

// Entry point of the pixel shader
PSOutput main(PSInput input)
{
    PSOutput output;
    
    // Use the interpolated color from the vertex shader
    output.Color = float4(input.Color, 1.0f);
    
    return output;
}
