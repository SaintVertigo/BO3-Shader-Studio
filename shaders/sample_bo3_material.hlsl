// BO3 HLSL Previewer - BO3 Material starter
// BO3_PREVIEWER_MATERIAL_SURFACE: EMISSIVE
//
// This is a real mesh/surface shader, not a fullscreen PostFX shader.
// Beginner mode should detect it as Material and open a 3D mesh preview.

#include "lib/globals.hlsl"
#include "lib/vertdecl_vertex.hlsl"
#include "lib/vertdecl_vertex_tangentspace.hlsl"
#include "lib/gpu_skin.hlsl"
#include "lib/gbuffer.hlsl"

static const float3 MATERIAL_BASE_COLOR = float3(0.10, 0.42, 0.82);
static const float MATERIAL_GRID_SCALE = 10.0;
static const float MATERIAL_GRID_STRENGTH = 0.16;

struct MaterialSurfaceInput
{
    float4 position      : SV_POSITION;
    float4 texCoords     : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 normal        : TEXCOORD2;
    float4 tangent       : TEXCOORD3;
    float4 biTangent     : TEXCOORD4;
};

MaterialSurfaceInput vs_main(const GBufferVertexInput vertex, const uint instance : INSTANCE_SEMANTIC)
{
    MaterialSurfaceInput output;

    float3 position = vertex.position;
    float3 normal = Vertex_DecodeNormal(vertex.normal);
    float3 tangent = Vertex_DecodeNormal(vertex.tangent.xyz);

    GPUSkin_SkinVertex(position, normal, tangent, vertex.weights, vertex.indices, instance);

    position = Transform_PositionToWorld(position, instance);
    normal = normalize(Transform_NormalToWorld(normal, instance));
    tangent = normalize(Transform_NormalToWorld(tangent, instance));
    float3 biTangent = Vertex_CalculateBiNormal(
        normal, tangent, Vertex_DecodeBiNormalSign(vertex.tangent.w));

    output.position = Transform_OffsetToClip(position);
    output.texCoords = float4(vertex.texCoords, 0.0, 0.0);
    output.worldPosition = float4(position, 1.0);
    output.normal = float4(normal, 0.0);
    output.tangent = float4(tangent, 0.0);
    output.biTangent = float4(biTangent, 0.0);
    return output;
}

float4 ps_main(const MaterialSurfaceInput input) : SV_TARGET0
{
    float2 uv = input.texCoords.xy;
    float3 n = normalize(input.normal.xyz);

    float2 cell = frac(uv * MATERIAL_GRID_SCALE);
    float gridLine = max(
        1.0 - smoothstep(0.035, 0.075, min(cell.x, 1.0 - cell.x)),
        1.0 - smoothstep(0.035, 0.075, min(cell.y, 1.0 - cell.y)));

    float lighting = 0.45 + 0.55 * saturate(dot(n, normalize(float3(0.35, 0.55, 0.75))));
    float3 color = MATERIAL_BASE_COLOR * lighting;
    color = lerp(color, color + 0.30, gridLine * MATERIAL_GRID_STRENGTH);

    return float4(saturate(color), 1.0);
}
