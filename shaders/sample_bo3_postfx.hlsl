#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);
SamplerState frameBufferSampler : register(s0);
SamplerState DepthSamplerState : register(s1);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

PS_INPUT vs_main(const VS_INPUT vertex, const uint instance : INSTANCE_SEMANTIC)
{
    PS_INPUT output;
    PostFx_GenerateFullscreenQuad(
        vertex.position, vertex.texcoord, instance, output.position, output.texcoord);
    return output;
}

float4 ps_main(PS_INPUT input) : SV_Target
{
    float2 uv = input.texcoord;
    float2 p = uv * 2.0 - 1.0;

    // Use BO3's runtime/TOOLSGFX helpers. These resolve to PerSceneConsts in
    // runtime and CodeSceneConstBuffer in the editor; no preview-only globals
    // are required.
    float t = GetTime();
    float2 invSize = PostFx_GetRenderTargetSize().zw;

    float3 color = PostFx_NormalizeColor(
        frameBuffer.Sample(frameBufferSampler, uv).rgb);
    float depth = DepthSampler.Sample(DepthSamplerState, uv).r;

    float vignette = saturate(1.0 - dot(p, p) * 0.35);
    float scan = 0.985 + 0.015 * sin((uv.y / max(invSize.y, 0.000001)) * 0.35 + t * 4.0);
    color *= vignette * scan;
    color += (depth - 0.5) * 0.02;

    return float4(PostFx_DenormalizeColor(color), 1.0);
}
