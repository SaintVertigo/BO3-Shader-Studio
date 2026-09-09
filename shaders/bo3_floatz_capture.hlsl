// BO3 Shader Studio — Ground-Truth Float-Z Capture
//
// Export/install this as a BO3 PostFX shader, activate it for one frame/scene,
// and save a LOSSLESS native-resolution PNG screenshot. Then use:
// Tools > BO3 Ground-Truth Float-Z Capture... > Import Capture PNG...
//
// The screen is intentionally divided into four aligned quadrants:
//   top-left     : resolvedScene color
//   top-right    : machine-readable logarithmic BO3 depth + viewmodel bit
//   bottom-left  : human-readable depth diagnostic
//   bottom-right : transfer calibration + zNear + format markers
//
// Do not crop, resize, JPEG-compress, or video-encode the screenshot. Hide HUD
// and third-party overlays if they are drawn after PostFX.

#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);

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

static const float BO3_CAPTURE_DEPTHHACK_SPLIT = 63.0 / 64.0;
static const float BO3_CAPTURE_DEPTH_LOG_MIN = -5.0;   // 0.03125
static const float BO3_CAPTURE_DEPTH_LOG_MAX = 17.0;   // 131072
static const float BO3_CAPTURE_ZNEAR_LOG_MIN = -12.0;
static const float BO3_CAPTURE_ZNEAR_LOG_MAX = 0.0;

// BO3 exposes the near clip through different symbols in the two shader
// compilation environments. Runtime HLSL uses PerSceneConsts.zNear, while
// TOOLSGFX uses CodeSceneConsts.gScene.nearClip. Keep one helper so this
// capture shader compiles in both the game and the APE/tools validation path.
float BO3CaptureGetZNear()
{
#if TOOLSGFX
    return max(gScene.nearClip, 0.000001);
#else
    return max(zNear.x, 0.000001);
#endif
}

uint BO3CaptureQuantize14(float normalizedValue)
{
    return (uint)round(saturate(normalizedValue) * 16383.0);
}

float3 BO3CaptureEncode14(uint code, bool highBit)
{
    uint r5 = code & 31u;
    uint g5 = (code >> 5u) & 31u;
    uint b5 = ((code >> 10u) & 15u) | (highBit ? 16u : 0u);
    return float3((float)r5, (float)g5, (float)b5) / 31.0;
}

float3 BO3CaptureEncodeDepth(float rawDepth)
{
    const bool viewmodel = rawDepth >= BO3_CAPTURE_DEPTHHACK_SPLIT;
    const float processed = FloatZ_Process(rawDepth);
    const float worldDistance = BO3CaptureGetZNear() / max(processed, 0.00000001);
    const float encoded = (log2(max(worldDistance, exp2(BO3_CAPTURE_DEPTH_LOG_MIN))) - BO3_CAPTURE_DEPTH_LOG_MIN) /
                          (BO3_CAPTURE_DEPTH_LOG_MAX - BO3_CAPTURE_DEPTH_LOG_MIN);
    return BO3CaptureEncode14(BO3CaptureQuantize14(encoded), viewmodel);
}

float3 BO3CaptureEncodeZNear()
{
    const float encoded = (log2(max(BO3CaptureGetZNear(), exp2(BO3_CAPTURE_ZNEAR_LOG_MIN))) - BO3_CAPTURE_ZNEAR_LOG_MIN) /
                          (BO3_CAPTURE_ZNEAR_LOG_MAX - BO3_CAPTURE_ZNEAR_LOG_MIN);
    return BO3CaptureEncode14(BO3CaptureQuantize14(encoded), false);
}

int2 BO3CaptureSourcePixel(float2 uv)
{
    const float2 sourceSize = max(PostFx_GetRenderTargetSize().xy, float2(1.0, 1.0));
    return int2(clamp(floor(saturate(uv) * sourceSize), float2(0.0, 0.0), sourceSize - 1.0));
}

float BO3CaptureRawDepthAt(float2 uv)
{
    return DepthSampler.Load(int3(BO3CaptureSourcePixel(uv), 0)).r;
}

float3 BO3CaptureDiagnostic(float rawDepth)
{
    const bool viewmodel = rawDepth >= BO3_CAPTURE_DEPTHHACK_SPLIT;
    const float processed = FloatZ_Process(rawDepth);
    const float worldDistance = BO3CaptureGetZNear() / max(processed, 0.00000001);
    const float d = saturate((log2(max(worldDistance, exp2(BO3_CAPTURE_DEPTH_LOG_MIN))) - BO3_CAPTURE_DEPTH_LOG_MIN) /
                             (BO3_CAPTURE_DEPTH_LOG_MAX - BO3_CAPTURE_DEPTH_LOG_MIN));
    float3 color = lerp(float3(0.015, 0.020, 0.030), float3(1.0, 1.0, 1.0), d);
    if(viewmodel)
        color = lerp(color, float3(1.0, 0.10, 0.85), 0.85);
    return color;
}

float3 BO3CaptureCalibration(float2 localUv)
{
    // 0..68%: 32-level neutral ramp. The importer measures these exact output
    // levels and uses them as a per-channel inverse LUT, so it does not need to
    // assume whether the screenshot path is linear, sRGB, or another monotonic
    // display transfer.
    if(localUv.y < 0.68)
    {
        uint level = (uint)min(floor(saturate(localUv.x) * 32.0), 31.0);
        return float3((float)level, (float)level, (float)level) / 31.0;
    }

    // 68..80%: two format/orientation magic markers.
    if(localUv.y < 0.80)
    {
        return localUv.x < 0.5
            ? float3(3.0, 27.0, 11.0) / 31.0
            : float3(29.0, 5.0, 23.0) / 31.0;
    }

    // 80..92%: captured BO3 zNear encoded with the same 14-bit packing.
    if(localUv.y < 0.92)
        return BO3CaptureEncodeZNear();

    // 92..100%: format version marker = v1.
    return float3(1.0, 19.0, 30.0) / 31.0;
}

float4 ps_main(PS_INPUT input) : SV_Target
{
    const float2 uv = saturate(input.texcoord);
    const bool right = uv.x >= 0.5;
    const bool bottom = uv.y >= 0.5;
    const float2 localUv = frac(uv * 2.0);

    float3 outColor = 0.0;

    if(!right && !bottom)
    {
        // Same-frame resolvedScene color. This quadrant becomes the preview t0.
        outColor = PostFx_NormalizeColor(frameBuffer.Load(int3(BO3CaptureSourcePixel(localUv), 0)).rgb);
    }
    else if(right && !bottom)
    {
        // Machine-readable Float-Z. 14 bits of logarithmic distance are packed
        // into 5/5/4 bits; the remaining B high bit stores BO3's >=63/64
        // depth-hack/viewmodel classification. 32 discrete output levels make
        // the encoding much more robust to the display transfer than raw bytes.
        outColor = BO3CaptureEncodeDepth(BO3CaptureRawDepthAt(localUv));
    }
    else if(!right && bottom)
    {
        // Human-readable sanity check: near is dark, far is bright, and BO3's
        // depth-hack/viewmodel range is magenta.
        outColor = BO3CaptureDiagnostic(BO3CaptureRawDepthAt(localUv));
    }
    else
    {
        outColor = BO3CaptureCalibration(localUv);
    }

    return float4(PostFx_DenormalizeColor(outColor), 1.0);
}
