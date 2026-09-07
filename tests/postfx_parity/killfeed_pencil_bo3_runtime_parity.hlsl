#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
SamplerState bilinearClampler : register(s1);

#define iTime (GetTime())
#define iTimeDelta (1.0 / 60.0)
#define iFrameRate 60.0
#define iFrame 0
#define iResolution (float3(PostFx_GetRenderTargetSize().xy, 1.0))
static const float4 iMouse = float4(0.0, 0.0, 0.0, 0.0);
static const float4 iDate = float4(0.0, 0.0, 0.0, 0.0);

// GLSL gl_FragCoord is a fragment-stage built-in visible from helper functions.
// ps_main updates this once per pixel before mainImage() runs.
static float4 GLSL_FRAGCOORD = float4(0.0, 0.0, 0.0, 1.0);

// GLSL square-matrix one-argument constructors need explicit compatibility.
// These helpers preserve diagonal scalar construction plus identity extension /
// upper-left truncation when converting between mat2/mat3/mat4.
float2x2 GLSL_MAT2(float s) { return float2x2(s, 0.0, 0.0, s); }
float2x2 GLSL_MAT2(float2x2 m) { return m; }
float2x2 GLSL_MAT2(float3x3 m) { return float2x2(m[0][0], m[0][1], m[1][0], m[1][1]); }
float2x2 GLSL_MAT2(float4x4 m) { return float2x2(m[0][0], m[0][1], m[1][0], m[1][1]); }

float3x3 GLSL_MAT3(float s) { return float3x3(s,0.0,0.0, 0.0,s,0.0, 0.0,0.0,s); }
float3x3 GLSL_MAT3(float2x2 m) { return float3x3(m[0][0],m[0][1],0.0, m[1][0],m[1][1],0.0, 0.0,0.0,1.0); }
float3x3 GLSL_MAT3(float3x3 m) { return m; }
float3x3 GLSL_MAT3(float4x4 m) { return float3x3(m[0][0],m[0][1],m[0][2], m[1][0],m[1][1],m[1][2], m[2][0],m[2][1],m[2][2]); }

float4x4 GLSL_MAT4(float s) { return float4x4(s,0.0,0.0,0.0, 0.0,s,0.0,0.0, 0.0,0.0,s,0.0, 0.0,0.0,0.0,s); }
float4x4 GLSL_MAT4(float2x2 m) { return float4x4(m[0][0],m[0][1],0.0,0.0, m[1][0],m[1][1],0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0); }
float4x4 GLSL_MAT4(float3x3 m) { return float4x4(m[0][0],m[0][1],m[0][2],0.0, m[1][0],m[1][1],m[1][2],0.0, m[2][0],m[2][1],m[2][2],0.0, 0.0,0.0,0.0,1.0); }
float4x4 GLSL_MAT4(float4x4 m) { return m; }

float2x2 GLSL_INVERSE(float2x2 m)
{
    float det = m[0][0]*m[1][1] - m[0][1]*m[1][0];
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float2x2(
         m[1][1], -m[0][1],
        -m[1][0],  m[0][0]) / det;
}
float3x3 GLSL_INVERSE(float3x3 m)
{
    float a=m[0][0], b=m[0][1], c=m[0][2];
    float d=m[1][0], e=m[1][1], f=m[1][2];
    float g=m[2][0], h=m[2][1], i=m[2][2];
    float det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float3x3(
        e*i-f*h, c*h-b*i, b*f-c*e,
        f*g-d*i, a*i-c*g, c*d-a*f,
        d*h-e*g, b*g-a*h, a*e-b*d) / det;
}
float GLSL_DET3(
    float a00,float a01,float a02,
    float a10,float a11,float a12,
    float a20,float a21,float a22)
{
    return a00*(a11*a22-a12*a21)
         - a01*(a10*a22-a12*a20)
         + a02*(a10*a21-a11*a20);
}
float4x4 GLSL_INVERSE(float4x4 m)
{
    float c00 = GLSL_DET3(m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]);
    float c01 = -(GLSL_DET3(m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]));
    float c02 = GLSL_DET3(m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]);
    float c03 = -(GLSL_DET3(m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]));
    float c10 = -(GLSL_DET3(m[0][1], m[0][2], m[0][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]));
    float c11 = GLSL_DET3(m[0][0], m[0][2], m[0][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]);
    float c12 = -(GLSL_DET3(m[0][0], m[0][1], m[0][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]));
    float c13 = GLSL_DET3(m[0][0], m[0][1], m[0][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    float c20 = GLSL_DET3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[3][1], m[3][2], m[3][3]);
    float c21 = -(GLSL_DET3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[3][0], m[3][2], m[3][3]));
    float c22 = GLSL_DET3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[3][0], m[3][1], m[3][3]);
    float c23 = -(GLSL_DET3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[3][0], m[3][1], m[3][2]));
    float c30 = -(GLSL_DET3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3]));
    float c31 = GLSL_DET3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3]);
    float c32 = -(GLSL_DET3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3]));
    float c33 = GLSL_DET3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]);
    float det = m[0][0]*c00 + m[0][1]*c01 + m[0][2]*c02 + m[0][3]*c03;
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float4x4(
        c00, c10, c20, c30,
        c01, c11, c21, c31,
        c02, c12, c22, c32,
        c03, c13, c23, c33) / det;
}

int2 GLSL_TEXTURE_SIZE(Texture2D<float4> tex, int lod)
{
    uint w=1, h=1, levels=1;
    tex.GetDimensions((uint)max(lod,0), w, h, levels);
    return int2(w,h);
}
int2 GLSL_TEXTURE_SIZE(TextureCube<float4> tex, int lod)
{
    uint w=1, h=1, levels=1;
    tex.GetDimensions((uint)max(lod,0), w, h, levels);
    return int2(w,h);
}


float3 GLSL_CHANNEL_RESOLUTION(int channel)
{
    return iResolution;
}

// GLSL permits vecN(scalar) splats and vector copy/truncation constructors.
// Do not overload these helpers: BO3's legacy FXC resolver can treat scalar/vector
// promotions as equally viable and report X3067 for calls such as vec3(0).
float2 GLSL_VEC2_S(float x)    { return float2(x, x); }
float2 GLSL_VEC2_V2(float2 x)  { return x; }
float2 GLSL_VEC2_V3(float3 x)  { return x.xy; }
float2 GLSL_VEC2_V4(float4 x)  { return x.xy; }
float3 GLSL_VEC3_S(float x)    { return float3(x, x, x); }
float3 GLSL_VEC3_V2(float2 x)  { return float3(x, 0.0); }
float3 GLSL_VEC3_V3(float3 x)  { return x; }
float3 GLSL_VEC3_V4(float4 x)  { return x.xyz; }
float4 GLSL_VEC4_S(float x)    { return float4(x, x, x, x); }
float4 GLSL_VEC4_V2(float2 x)  { return float4(x, 0.0, 0.0); }
float4 GLSL_VEC4_V3(float3 x)  { return float4(x, 0.0); }
float4 GLSL_VEC4_V4(float4 x)  { return x; }

int2 GLSL_IVEC2_S(int x)      { return int2(x, x); }
int2 GLSL_IVEC2_V2(int2 x)    { return x; }
int2 GLSL_IVEC2_V3(int3 x)    { return x.xy; }
int2 GLSL_IVEC2_V4(int4 x)    { return x.xy; }
int3 GLSL_IVEC3_S(int x)      { return int3(x, x, x); }
int3 GLSL_IVEC3_V2(int2 x)    { return int3(x, 0); }
int3 GLSL_IVEC3_V3(int3 x)    { return x; }
int3 GLSL_IVEC3_V4(int4 x)    { return x.xyz; }
int4 GLSL_IVEC4_S(int x)      { return int4(x, x, x, x); }
int4 GLSL_IVEC4_V2(int2 x)    { return int4(x, 0, 0); }
int4 GLSL_IVEC4_V3(int3 x)    { return int4(x, 0); }
int4 GLSL_IVEC4_V4(int4 x)    { return x; }

uint2 GLSL_UVEC2_S(uint x)    { return uint2(x, x); }
uint2 GLSL_UVEC2_V2(uint2 x)  { return x; }
uint2 GLSL_UVEC2_V3(uint3 x)  { return x.xy; }
uint2 GLSL_UVEC2_V4(uint4 x)  { return x.xy; }
uint3 GLSL_UVEC3_S(uint x)    { return uint3(x, x, x); }
uint3 GLSL_UVEC3_V2(uint2 x)  { return uint3(x, 0); }
uint3 GLSL_UVEC3_V3(uint3 x)  { return x; }
uint3 GLSL_UVEC3_V4(uint4 x)  { return x.xyz; }
uint4 GLSL_UVEC4_S(uint x)    { return uint4(x, x, x, x); }
uint4 GLSL_UVEC4_V2(uint2 x)  { return uint4(x, 0, 0); }
uint4 GLSL_UVEC4_V3(uint3 x)  { return uint4(x, 0); }
uint4 GLSL_UVEC4_V4(uint4 x)  { return x; }

bool2 GLSL_BVEC2_S(bool x)    { return bool2(x, x); }
bool2 GLSL_BVEC2_V2(bool2 x)  { return x; }
bool2 GLSL_BVEC2_V3(bool3 x)  { return x.xy; }
bool2 GLSL_BVEC2_V4(bool4 x)  { return x.xy; }
bool3 GLSL_BVEC3_S(bool x)    { return bool3(x, x, x); }
bool3 GLSL_BVEC3_V2(bool2 x)  { return bool3(x, false); }
bool3 GLSL_BVEC3_V3(bool3 x)  { return x; }
bool3 GLSL_BVEC3_V4(bool4 x)  { return x.xyz; }
bool4 GLSL_BVEC4_S(bool x)    { return bool4(x, x, x, x); }
bool4 GLSL_BVEC4_V2(bool2 x)  { return bool4(x, false, false); }
bool4 GLSL_BVEC4_V3(bool3 x)  { return bool4(x, false); }
bool4 GLSL_BVEC4_V4(bool4 x)  { return x; }

// GLSL component-wise relational builtins. HLSL comparison operators already
// return boolN for vector operands, so macros preserve the GLSL result width
// without legacy-FXC overload ambiguity.
#define GLSL_LESS_THAN(x, y) ((x) < (y))
#define GLSL_LESS_THAN_EQUAL(x, y) ((x) <= (y))
#define GLSL_GREATER_THAN(x, y) ((x) > (y))
#define GLSL_GREATER_THAN_EQUAL(x, y) ((x) >= (y))
#define GLSL_EQUAL(x, y) ((x) == (y))
#define GLSL_NOT_EQUAL(x, y) ((x) != (y))
#define GLSL_NOT(x) (!(x))

#define GLSL_TEXTURE(tex, uv) tex.Sample(bilinearClampler, (uv))
#define GLSL_TEXTURE_BIAS(tex, uv, bias) tex.SampleBias(bilinearClampler, (uv), (bias))
#define GLSL_TEXTURE_LEVEL(tex, uv, lod) tex.SampleLevel(bilinearClampler, (uv), (lod))
#define GLSL_TEXTURE_GRAD(tex, uv, dx, dy) tex.SampleGrad(bilinearClampler, (uv), (dx), (dy))
#define GLSL_TEXTURE_S(tex, samp, uv) tex.Sample(samp, (uv))
#define GLSL_TEXTURE_BIAS_S(tex, samp, uv, bias) tex.SampleBias(samp, (uv), (bias))
#define GLSL_TEXTURE_LEVEL_S(tex, samp, uv, lod) tex.SampleLevel(samp, (uv), (lod))
#define GLSL_TEXTURE_GRAD_S(tex, samp, uv, dx, dy) tex.SampleGrad(samp, (uv), (dx), (dy))
#define GLSL_TEXEL_FETCH(tex, p, lod) tex.Load(int3(int2(p), int(lod)))
float GLSL_MOD(float x, float y) { return x - y * floor(x / y); }
float2 GLSL_MOD(float2 x, float2 y) { return x - y * floor(x / y); }
float3 GLSL_MOD(float3 x, float3 y) { return x - y * floor(x / y); }
float4 GLSL_MOD(float4 x, float4 y) { return x - y * floor(x / y); }
float2 GLSL_MOD(float2 x, float y) { return x - y * floor(x / y); }
float3 GLSL_MOD(float3 x, float y) { return x - y * floor(x / y); }
float4 GLSL_MOD(float4 x, float y) { return x - y * floor(x / y); }

// Relaxed scalar conversion for real-world shader-golf sources. Valid scalar
// GLSL int(...) casts preserve normal behavior; vector inputs select x so
// otherwise-undefined vector-to-scalar source remains deterministic in BO3.
int GLSL_INT(float x) { return (int)x; }
int GLSL_INT(int x) { return x; }
int GLSL_INT(uint x) { return (int)x; }
int GLSL_INT(bool x) { return x ? 1 : 0; }
int GLSL_INT(float2 x) { return (int)x.x; }
int GLSL_INT(float3 x) { return (int)x.x; }
int GLSL_INT(float4 x) { return (int)x.x; }
int GLSL_INT(int2 x) { return x.x; }
int GLSL_INT(int3 x) { return x.x; }
int GLSL_INT(int4 x) { return x.x; }
int GLSL_INT(uint2 x) { return (int)x.x; }
int GLSL_INT(uint3 x) { return (int)x.x; }
int GLSL_INT(uint4 x) { return (int)x.x; }
int GLSL_INT(bool2 x) { return x.x ? 1 : 0; }
int GLSL_INT(bool3 x) { return x.x ? 1 : 0; }
int GLSL_INT(bool4 x) { return x.x ? 1 : 0; }

int GLSL_WRAP_INDEX_2(int i) { int j = i % 2; return j < 0 ? j + 2 : j; }
int GLSL_WRAP_INDEX_3(int i) { int j = i % 3; return j < 0 ? j + 3 : j; }
int GLSL_WRAP_INDEX_4(int i) { int j = i % 4; return j < 0 ? j + 4 : j; }
void GLSL_SET_VEC2(inout float2 v, int i, float x) { i=GLSL_WRAP_INDEX_2(i); if(i==0) v.x=x; else v.y=x; }
void GLSL_SET_VEC3(inout float3 v, int i, float x) { i=GLSL_WRAP_INDEX_3(i); if(i==0) v.x=x; else if(i==1) v.y=x; else v.z=x; }
void GLSL_SET_VEC4(inout float4 v, int i, float x) { i=GLSL_WRAP_INDEX_4(i); if(i==0) v.x=x; else if(i==1) v.y=x; else if(i==2) v.z=x; else v.w=x; }


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

// -----------------------------------------------------------------------------
// Converted GLSL
// -----------------------------------------------------------------------------
// Faithful pencil tuning controls.
// These keep the original pencil structure, but stabilize BO3 lighting.
// -----------------------------------------------------------------------------
static const float KF_PENCIL_SATURATION   = 1.88;
static const float KF_PAPER_FLOOR         = 5.56;
static const float KF_PAPER_TINT_R        = 1.00;
static const float KF_PAPER_TINT_G        = 1.00;
static const float KF_PAPER_TINT_B        = 1.00;
static const float KF_VIGNETTE_STRENGTH   = 0.04;
static const float KF_FINAL_CONTRAST      = 1.06;
static const float KF_FINAL_PIVOT         = 0.40;
static const float KF_INK_POWER           = 1.60;

// BO3 lighting stabilization
static const float KF_SOURCE_EXPOSURE     = 1.20;
static const float KF_SOURCE_SHADOW_LIFT  = 0.24;
static const float KF_SOURCE_COLOR_KEEP   = 10.00;

// Half-tone / paper response
static const float KF_HT_CENTER_LOW       = 0.74;
static const float KF_HT_CENTER_HIGH      = 0.92;
static const float KF_HT_SPAN             = 0.10;
static const float KF_HT_NOISE            = 0.75;


// BO3 single-input version:
// frameBuffer (t0) is the actual game/source image.
// No auxiliary image textures or procedural noise are required.
#define Res0 iResolution.xy
#define Res1 iResolution.xy
#define Res  iResolution.xy

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return frac(p.x * p.y);
}

float3 ToneCompressScene(float3 rgb)
{
    rgb = max(rgb * KF_SOURCE_EXPOSURE, 0.0);

    float luma = max(dot(rgb, float3(0.299, 0.587, 0.114)), 0.0001);

    // Compress bright scenes (flashlight on), but keep dark scenes readable.
    float mapped = luma / (1.0 + luma);

    // Lift darker scenes a bit without making everything flat.
    float shadowLift = KF_SOURCE_SHADOW_LIFT * (1.0 - mapped) * saturate(1.0 - luma);
    mapped = saturate(mapped + shadowLift);

    float scale = mapped / max(luma, 0.02);
    rgb *= scale;

    // Keep some color, but make it behave more like paper pigment.
    float3 neutral = float3(mapped, mapped, mapped);
    rgb = lerp(neutral, rgb, KF_SOURCE_COLOR_KEEP);

    return saturate(rgb);
}


float4 getCol(float2 pos)
{
    float2 uv = saturate(pos / iResolution.xy);

    float4 c1 = frameBuffer.Sample(bilinearClampler, uv);
    c1.rgb = PostFx_NormalizeColor(c1.rgb);
    c1.rgb = ToneCompressScene(c1.rgb);

    float d = clamp(dot(c1.xyz, float3(-0.5, 1.0, -0.5)), 0.0, 1.0);
    float3 c2 = float3(0.7, 0.7, 0.7);

    float3 outCol = lerp(c1.rgb, c2, 1.8 * d);
    outCol = min(outCol, float3(0.7, 0.7, 0.7));

    return float4(outCol, 1.0);
}

float4 getColHT(float2 pos)
{
    float4 c = getCol(pos);

    // Recreate the original GLSL idea:
    // c * 0.8 + 0.2 + noise
    float n = hash21(pos * 0.0017);
    float4 noise4 = float4(n, n, n, n);

    float luma = saturate(dot(c.rgb, float3(0.299, 0.587, 0.114)));
    float center = lerp(KF_HT_CENTER_LOW, KF_HT_CENTER_HIGH, luma);

    float lowT  = center - KF_HT_SPAN;
    float highT = center + KF_HT_SPAN;

    float4 htSrc = c * 0.8 + 0.2 + (noise4 - 0.5) * KF_HT_NOISE;

    return smoothstep(
        float4(lowT,  lowT,  lowT,  lowT),
        float4(highT, highT, highT, highT),
        htSrc
    );
}

float getVal(float2 pos)
{
    float4 c=getCol(pos);
 	return pow(dot(c.xyz,GLSL_VEC3_S((float)(.333))),1.)*1.;
}

float2 getGrad(float2 pos, float eps)
{
   	float2 d=float2(eps,0);
    return float2(
        getVal(pos+d.xy)-getVal(pos-d.xy),
        getVal(pos+d.yx)-getVal(pos-d.yx)
    )/eps/2.;
}

#define AngleNum 3

#define SampNum 16
#define PI2 6.28318530717959

void mainImage( out float4 fragColor, in float2 fragCoord )
{
    fragColor = (float4)0;
    // Fixed post-FX sampling position: removed the original time-based camera/screen drift.
    float2 pos = fragCoord;
    float3 col = GLSL_VEC3_S((float)(0));
    float3 col2 = GLSL_VEC3_S((float)(0));
    float sum=0.;
    [loop]
    for(int i=0;i<AngleNum;i++)
    {
        float ang=PI2/float(AngleNum)*(float(i)+.8);
        float2 v=float2(cos(ang),sin(ang));
        [loop]
        for(int j=0;j<SampNum;j++)
        {
            float2 dpos  = v.yx*float2(1,-1)*float(j)*iResolution.y/400.;
            float2 dpos2 = v.xy*float(j*j)/float(SampNum)*.5*iResolution.y/400.;
	        float2 g = (float2)0;
            float fact = (float)0;
            float fact2 = (float)0;

            [unroll]
            for(float s=-1.;s<=1.;s+=2.)
            {
                float2 pos2=pos+s*dpos+dpos2;
                float2 pos3=pos+(s*dpos+dpos2).yx*float2(1,-1)*2.;
            	g=getGrad(pos2,.4);
            	fact=dot(g,v)-.5*abs(dot(g,v.yx*float2(1,-1))) ;
            	fact2=dot(normalize(g+GLSL_VEC2_S((float)(.0001))),v.yx*float2(1,-1));
                
                fact=clamp(fact,0.,.05);
                fact2=abs(fact2);
                
                fact*=1.-float(j)/float(SampNum);
            	col += fact;
            	col2 += fact2*getColHT(pos3).xyz;
            	sum+=fact2;
            }
        }
    }
    col/=float(SampNum*AngleNum)*.75/sqrt(iResolution.y);
    if (sum > 0.0001)
    {
        col2 /= sum;
    }
    else
    {
        col2 = getCol(pos).xyz;
    }
    col.x=1.-col.x;
    col.x*=col.x*col.x;

    float3 karo = float3(1.0, 1.0, 1.0);

    // Keep the original color accumulator, but make it behave more like pigment
    // on paper instead of turning low-light areas into solid black blocks.
    float col2Luma = dot(col2, float3(0.299, 0.587, 0.114));
    float3 col2Desat = lerp(float3(col2Luma, col2Luma, col2Luma), col2, KF_PENCIL_SATURATION);

    float3 paperTint = float3(KF_PAPER_TINT_R, KF_PAPER_TINT_G, KF_PAPER_TINT_B);
    float3 pigmentBase = lerp(paperTint * KF_PAPER_FLOOR, col2Desat * 0.92 + paperTint * 0.08, 0.92);

    float r = length(pos - iResolution.xy * 0.5) / iResolution.x;
    float vign = 1.0 - KF_VIGNETTE_STRENGTH * r * r * r;

    float inkResponse = pow(saturate(col.x), KF_INK_POWER);

    // Keep the original pencil response, but prevent total black collapse.
    float3 pencilColor = pigmentBase * inkResponse;
    pencilColor = lerp(paperTint * KF_PAPER_FLOOR, pencilColor, 0.93);
    pencilColor *= karo * vign;

    // Small paper floor so "lights off" doesn't vanish into black.
    pencilColor = max(pencilColor, paperTint * 0.090 * (1.0 - inkResponse));

    // Final contrast after stabilization, not before.
    pencilColor = (pencilColor - KF_FINAL_PIVOT) * KF_FINAL_CONTRAST + KF_FINAL_PIVOT;

    fragColor = float4(saturate(pencilColor), 1.0);
    
}

float4 ps_main(const PixelInput input) : SV_TARGET0
{
    // Stock BO3 fullscreen postfx shaders consume TEXCOORD0 directly.
    float2 uv = saturate(input.texCoords);
    float2 fragCoord = uv * iResolution.xy;

    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);

    // All pencil math runs in the same normalized color space as TOOLSGFX.
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(fragColor, fragCoord);

    // TOOLSGFX is identity; BO3 runtime restores the engine postfx scale.
    fragColor.rgb = PostFx_DenormalizeColor(fragColor.rgb);
    return fragColor;
}
