// Heavy pixel shader for GPU load testing
// Performs configurable iterations that cannot be optimized away by the compiler

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

// Constant buffer for pixel shader parameters
cbuffer PixelParams : register(b1)
{
    uint IterationCount;
    uint3 _padding;  // Align to 16 bytes
};

// Entry point of the pixel shader
PSOutput main(PSInput input)
{
    PSOutput output;
    float3 color = input.Color;
    
    // Seed with per-pixel data to prevent cross-pixel optimization
    // Using screen position ensures each pixel has unique computation
    float accumulator = frac(input.Position.x * 0.00123f + input.Position.y * 0.00071f);
    
    // Heavy loop that cannot be optimized away:
    // - [loop] attribute hints to prevent unrolling
    // - Each iteration depends on previous (data dependency chain)
    // - Transcendental functions (sin, cos) prevent compile-time evaluation
    // - Per-pixel seed prevents uniform optimization
    [loop]
    for (uint i = 0; i < IterationCount; i++)
    {
        float x = accumulator + float(i) * 0.01f;
        accumulator = sin(x) * cos(x * 1.1f) + accumulator * 0.5f;
    }
    
    // Result must affect output or compiler eliminates the loop as dead code
    // Using tiny multiplier keeps visual impact negligible
    color = saturate(color + accumulator * 0.00001f);
    
    output.Color = float4(color, 1.0f);
    return output;
}
