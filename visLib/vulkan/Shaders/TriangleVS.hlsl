// TriangleVS.hlsl
// Hardcoded 3-vertex triangle. No vertex buffer or constant buffer required —
// SV_VertexID indexes into compile-time-constant arrays.

static const float2 g_positions[3] = {
    float2( 0.0f,  0.5f),
    float2(-0.5f, -0.5f),
    float2( 0.5f, -0.5f),
};

static const float3 g_colors[3] = {
    float3(1.0f, 0.0f, 0.0f),
    float3(0.0f, 1.0f, 0.0f),
    float3(0.0f, 0.0f, 1.0f),
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput o;
    o.position = float4(g_positions[vertexId], 0.0f, 1.0f);
    o.color    = g_colors[vertexId];
    return o;
}
