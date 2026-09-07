// BO3 HLSL Previewer - Generic HLSL starter
//
// This is intentionally generic Direct3D HLSL. It is useful for syntax and
// pixel-shader experimentation, but it is not a BO3 package by itself.

float4 ps_main(float4 position : SV_POSITION) : SV_TARGET0
{
    float2 uv = frac(position.xy / float2(640.0, 360.0));
    float checker = fmod(floor(uv.x * 12.0) + floor(uv.y * 8.0), 2.0);
    float3 a = float3(0.035, 0.12, 0.26);
    float3 b = float3(0.72, 0.18, 0.11);
    float3 color = lerp(a, b, uv.x);
    color *= lerp(0.72, 1.0, checker);
    color += float3(0.06, 0.08, 0.10) * uv.y;
    return float4(saturate(color), 1.0);
}
