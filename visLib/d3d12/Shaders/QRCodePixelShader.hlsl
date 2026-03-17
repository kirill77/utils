// QR-code pixel shader for test pattern rendering
// Renders a real QR code from packed module data with configurable GPU load iterations

// Input structure from the vertex shader
struct PSInput
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR;
    float2 UV : TEXCOORD;
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
    uint QRSize;          // QR code side length (21 for Version 1)
    uint2 _padding;       // Align to 16 bytes
    uint QRData[16];      // Packed QR module bits (row-major, bit 0 of word 0 = [0][0])
};

// Read a single module from the packed QR data
bool getQRModule(int row, int col)
{
    int bitIdx = row * (int)QRSize + col;
    uint word = QRData[bitIdx / 32];
    return (word >> (bitIdx % 32)) & 1;
}

// Entry point of the pixel shader
PSOutput main(PSInput input)
{
    PSOutput output;

    // --- QR code pattern ---
    // Use UV coordinates (0..1) to map into the QR module grid
    // This makes the pattern move with the geometry
    int qrCol = clamp((int)(input.UV.x * (float)QRSize), 0, (int)QRSize - 1);
    int qrRow = clamp((int)(input.UV.y * (float)QRSize), 0, (int)QRSize - 1);

    // Look up module value
    // Dark modules = black, light modules = colored by UV-derived gradient
    bool isDark = getQRModule(qrRow, qrCol);
    float3 uvColor = float3(input.UV.x, input.UV.y, 1.0f - 0.5f * (input.UV.x + input.UV.y));
    float3 qrColor = isDark ? float3(0.0f, 0.0f, 0.0f) : uvColor;

    // --- Configurable GPU load ---
    // Seed with per-pixel data to prevent cross-pixel optimization
    float accumulator = frac(input.Position.x * 0.00123f + input.Position.y * 0.00071f);

    [loop]
    for (uint i = 0; i < IterationCount; i++)
    {
        float x = accumulator + float(i) * 0.01f;
        accumulator = sin(x) * cos(x * 1.1f) + accumulator * 0.5f;
    }

    // Result must affect output to prevent dead-code elimination
    // Tiny multiplier keeps visual impact negligible
    qrColor = saturate(qrColor + accumulator * 0.00001f);

    output.Color = float4(qrColor, 1.0f);
    return output;
}
