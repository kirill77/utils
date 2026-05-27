// QRCodePS.hlsl - Vulkan port of d3d12/Shaders/QRCodePixelShader.hlsl
// Renders a real QR code from packed module data with configurable GPU load.

struct PSInput {
    float4 Position : SV_Position;
    float3 Color    : COLOR;
    float2 UV       : TEXCOORD;
};

struct PSOutput {
    float4 Color : SV_Target;
};

// Set 0, binding 1: pixel shader parameters (per-frame).
[[vk::binding(1, 0)]]
cbuffer PixelParams {
    uint IterationCount;     // Legacy (unused) - iteration count now comes from push constant.
    uint QRSize;             // QR code side length (21 for Version 1).
    uint2 _padding;          // Align to 16 bytes.
    uint QRData[16];         // Packed QR module bits.
};

// Push constants: World at offset 0 (64 bytes, vertex), IterCount at offset 64 (4 bytes, pixel).
// World layout must match MeshVS (column-major default).
struct PushConstants {
    matrix World;
    uint PerObjectIterationCount;
};
[[vk::push_constant]] PushConstants pushConsts;

bool getQRModule(int row, int col) {
    int bitIdx = row * (int)QRSize + col;
    uint word = QRData[bitIdx / 32];
    return (word >> (bitIdx % 32)) & 1;
}

PSOutput main(PSInput input) {
    PSOutput output;

    int qrCol = clamp((int)(input.UV.x * (float)QRSize), 0, (int)QRSize - 1);
    int qrRow = clamp((int)(input.UV.y * (float)QRSize), 0, (int)QRSize - 1);

    bool isDark = getQRModule(qrRow, qrCol);
    float3 uvColor = float3(input.UV.x, input.UV.y, 1.0f - 0.5f * (input.UV.x + input.UV.y));
    float3 qrColor = isDark ? float3(0.0f, 0.0f, 0.0f) : uvColor;

    // Configurable GPU load (matches D3D12 behavior).
    float accumulator = frac(input.Position.x * 0.00123f + input.Position.y * 0.00071f);

    [loop]
    for (uint i = 0; i < pushConsts.PerObjectIterationCount; i++) {
        float x = accumulator + float(i) * 0.01f;
        accumulator = sin(x) * cos(x * 1.1f) + accumulator * 0.5f;
    }

    qrColor = saturate(qrColor + accumulator * 0.00001f);

    output.Color = float4(qrColor, 1.0f);
    return output;
}
