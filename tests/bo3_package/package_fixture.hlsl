Texture2D<float4> frameBuffer : register(t0);
SamplerState bilinearClampler : register(s1);

float4 tint;

struct FixtureVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FixtureVertex vs_main(uint vertexId : SV_VertexID)
{
    FixtureVertex output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 ps_main(FixtureVertex input) : SV_TARGET0
{
    return frameBuffer.Sample(bilinearClampler, input.uv) * tint;
}
