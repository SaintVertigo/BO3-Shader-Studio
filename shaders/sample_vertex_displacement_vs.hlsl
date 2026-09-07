// BO3 HLSL Previewer - vertex-only displacement sample
// Load a color texture into Color / Albedo (t0).
// Load a height map into Height / POM (t3).
// This file intentionally has vs_main and NO ps_main. The previewer supplies
// its built-in material/GBuffer pixel shader automatically.

Texture2D<float4> heightMap : register(t3);
SamplerState materialSampler : register(s0);

cbuffer PreviewTransform : register(b0)
{
    float4x4 worldViewProj;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float2 texcoord0 : TEXCOORD0;
};

VS_OUT vs_main(VS_IN i)
{
    VS_OUT o;
    float h = heightMap.SampleLevel(materialSampler, i.texcoord, 0.0).r;
    float3 displaced = i.position + i.normal * ((h - 0.5) * 0.55);
    o.position = mul(float4(displaced, 1.0), worldViewProj);
    o.texcoord0 = i.texcoord;
    return o;
}
