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

float4 ps_main(const PixelInput input) : SV_TARGET0
{
    float2 uv = saturate(input.texCoords);
    float v = saturate(uv.x);

    // Known normalized values. These are deliberately generated instead of
    // sampling resolvedScene so a BO3 screenshot measures the downstream
    // runtime/display transfer applied after this postfx pass.
    float3 c;
    if (uv.y < 0.20)
        c = v.xxx;                // grayscale ramp
    else if (uv.y < 0.40)
        c = float3(v, 0.0, 0.0); // red ramp
    else if (uv.y < 0.60)
        c = float3(0.0, v, 0.0); // green ramp
    else if (uv.y < 0.80)
        c = float3(0.0, 0.0, v); // blue ramp
    else
    {
        // Fixed reference steps: 0, .125, .25, .5, .75, 1.0.
        const float stepIndex = floor(uv.x * 6.0);
        if (stepIndex < 1.0)      c = 0.0.xxx;
        else if (stepIndex < 2.0) c = 0.125.xxx;
        else if (stepIndex < 3.0) c = 0.25.xxx;
        else if (stepIndex < 4.0) c = 0.50.xxx;
        else if (stepIndex < 5.0) c = 0.75.xxx;
        else                      c = 1.0.xxx;
    }

    // Keep the runtime frameBuffer + sampler alive in optimized FXC bytecode.
    // The working techset binds frameBuffer=CodeTexture("resolvedScene"); if the
    // probe never consumes that resource, FXC can strip both parameters and BO3
    // may reject/skip the technique even though raw HLSL compilation succeeds.
    // Sampling alpha does not alter the calibration RGB ramp.
    float keepAliveAlpha = frameBuffer.Sample(bilinearClampler, uv).a;

    return float4(PostFx_DenormalizeColor(c), keepAliveAlpha);
}
