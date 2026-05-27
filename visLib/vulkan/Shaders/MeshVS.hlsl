// MeshVS.hlsl - Vulkan port of d3d12/Shaders/VertexShader.hlsl
// Transforms vertex through World (push constant) -> View -> Projection.

struct VSInput {
    float3 Position : POSITION;
    float2 UV       : TEXCOORD;
};

struct VSOutput {
    float4 Position : SV_Position;
    float3 Color    : COLOR;
    float2 UV       : TEXCOORD;
};

// Set 0, binding 0: view + projection matrices (per-frame).
// No row_major qualifier — DXC/SPIR-V defaults to column-major to match
// D3D12 cbuffer behavior. The CPU writes XMMATRIX-style bytes (visLib's
// affine3 row layout); the shader interprets each "row" as a column, which
// is exactly what mul(matrix, columnVec) expects.
[[vk::binding(0, 0)]]
cbuffer TransformCB {
    matrix View;
    matrix Projection;
};

// Push constants: per-object world matrix at offset 0,
// per-object iteration count at offset 64 (used by the pixel shader).
struct PushConstants {
    matrix World;
    uint PerObjectIterationCount;
};
[[vk::push_constant]] PushConstants pushConsts;

VSOutput main(VSInput input) {
    VSOutput output;

    float4 worldPosition = mul(pushConsts.World, float4(input.Position, 1.0f));
    float4 viewPosition  = mul(View, worldPosition);
    output.Position      = mul(Projection, viewPosition);

    // Simple position-based vertex color (matches D3D12 VertexShader behavior).
    output.Color = normalize(input.Position) * 0.5f + 0.5f;
    output.UV    = input.UV;

    return output;
}
