// Text rendering pixel shader
// Input structure from the vertex shader
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

// Output structure
struct PSOutput
{
    float4 Color : SV_TARGET;
};

// Font atlas texture
Texture2D FontAtlas : register(t0);
SamplerState FontSampler : register(s0);

// Text parameters
cbuffer TextParams : register(b0)
{
    float4 TextColor;
    float2 ScreenSize;
    float2 Padding;
};

// Entry point of the pixel shader
PSOutput main(PSInput input)
{
    PSOutput output;
    
    // Sample the font atlas
    float4 atlasColor = FontAtlas.Sample(FontSampler, input.TexCoord);
    
    // Use the alpha channel from the atlas as the text opacity
    // The RGB channels are white in our font atlas setup
    float textAlpha = atlasColor.a;
    
    // Apply text color with atlas alpha
    output.Color = float4(TextColor.rgb, TextColor.a * textAlpha);
    
    return output;
}
