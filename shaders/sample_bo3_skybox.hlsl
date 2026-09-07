// BO3 HLSL Previewer - BO3 Skybox starter
//
// This shader consumes a world/sky direction. Beginner mode should detect it
// as Skybox and enable directional camera navigation automatically.

static const float3 SKY_ZENITH = float3(0.025, 0.075, 0.20);
static const float3 SKY_HORIZON = float3(0.46, 0.22, 0.12);
static const float3 SKY_GROUND = float3(0.012, 0.018, 0.028);

struct PixelShaderInput
{
    float4 position     : SV_POSITION0;
    float4 skyDirection : TEXCOORD0;
    float4 fogDirection : TEXCOORD1;
};

PixelShaderInput vs_main(uint vertexId : SV_VertexID)
{
    PixelShaderInput output;
    float2 clip = float2(
        vertexId == 2 ? 3.0 : -1.0,
        vertexId == 1 ? 3.0 : -1.0);
    output.position = float4(clip, 0.0, 1.0);

    // A simple direction field for a fullscreen sky triangle. BO3's sky state
    // can use this self-contained VS/PS pair without relying on an unbundled
    // stock vs_sky stage, which keeps package validation deterministic.
    float3 direction = normalize(float3(clip.x, 1.35, -clip.y));
    output.skyDirection = float4(direction, 0.0);
    output.fogDirection = float4(direction, 0.0);
    return output;
}

float hash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float4 ps_main(const PixelShaderInput input) : SV_TARGET0
{
    float3 d = normalize(input.skyDirection.xyz);
    float horizon = saturate(1.0 - abs(d.z));
    float up = saturate(d.z * 0.5 + 0.5);

    float3 color = lerp(SKY_GROUND, SKY_ZENITH, smoothstep(0.0, 0.62, up));
    color = lerp(color, SKY_HORIZON, pow(horizon, 5.0) * 0.72);

    float3 starCell = floor(d * 420.0);
    float star = step(0.9975, hash31(starCell)) * smoothstep(0.15, 0.70, d.z);
    color += star * float3(0.75, 0.86, 1.0);

    return float4(color, 1.0);
}
