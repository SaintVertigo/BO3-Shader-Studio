#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
SamplerState bilinearClampler : register(s1);

struct VertexInput
{
    float3 position : POSITION;
    float2 texCoords : TEXCOORD0;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texCoords : TEXCOORD0;
};

PixelInput vs_main(const VertexInput vertex, const uint instance : INSTANCE_SEMANTIC)
{
    PixelInput pixel;
    PostFx_GenerateFullscreenQuad(vertex.position, vertex.texCoords, instance, pixel.position, pixel.texCoords);
    return pixel;
}

float3 EncodeResolvedScene(float3 normalizedScene)
{
    normalizedScene = max(normalizedScene, 0.0);

    // Reversible HDR compression. After correcting for the output transfer
    // measured by probe 01, recover normalizedScene with:
    //     scene = encoded / max(1.0 - encoded, epsilon)
    return normalizedScene / (1.0 + normalizedScene);
}

float4 ps_main(const PixelInput input) : SV_TARGET0
{
    float2 uv = saturate(input.texCoords);
    float3 normalizedScene = PostFx_NormalizeColor(
        frameBuffer.Sample(bilinearClampler, uv).rgb
    );

    float3 encoded = EncodeResolvedScene(normalizedScene);
    return float4(PostFx_DenormalizeColor(encoded), 1.0);
}
