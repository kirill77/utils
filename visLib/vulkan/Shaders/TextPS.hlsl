// TextPS.hlsl - Vulkan port of d3d12/Shaders/TextPixelShader.hlsl

struct PSInput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD;
};

struct PSOutput {
    float4 Color : SV_Target;
};

[[vk::binding(0, 0)]]
cbuffer TextParams {
    float4 TextColor;
    float2 ScreenSize;
    float2 _padding;
};

// Combined image sampler at set 0, binding 1.
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2D    FontAtlas;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState FontSampler;

PSOutput main(PSInput input) {
    PSOutput output;

    float4 atlasColor = FontAtlas.Sample(FontSampler, input.TexCoord);
    float textAlpha = atlasColor.a;

    output.Color = float4(TextColor.rgb, TextColor.a * textAlpha);
    return output;
}
