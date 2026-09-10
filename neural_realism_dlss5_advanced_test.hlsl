// ============================================================================
// Neural Realism -- advanced deterministic BO3 PostFX experiment
// NOT NVIDIA DLSS 5, neural inference, or physically based reconstruction.
// Cost / processed pixel: 49 scene samples; optional 49 point depth loads.
// Original half / strength zero: 1 scene sample. No history or extra passes.
// Revision 6: independent broad-tone response, separate from texture protection.
// Three nested edge-aware estimates at 1, 4, 14 pixel radii (at 1080p),
// log-luminance frequency recombination, paired geometry and appearance cues.
// Open directly in Studio Advanced / PostFX. No Studio changes required.
// BO3 export still needs the Studio-generated techset / normal PostFX binding.
// Inputs: resolvedScene t0, floatZ t1, frameBufferSampler s0. No GBuffer inputs.
// Research motivation (visual goals only):
// https://research.nvidia.com/labs/adlr/DLSS5/
// https://www.nvidia.com/en-eu/geforce/news/dlss-5-3d-guided-neural-rendering/
// Color math uses normalized BO3 linear color, NOT an extra gamma conversion.
// No UV displacement, invented reflection image, random noise or temporal state.
// Identical inputs produce identical outputs; motion stability is not temporal AA.
// ============================================================================

#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);
SamplerState frameBufferSampler : register(s0);

static const float NR_STRENGTH            = 1.00; // 0..1; zero is exact bypass
static const float NR_STRUCTURE_INTENSITY = 2.00; // 0..3; scales band budgets too
static const float NR_TONE_INTENSITY      = 1.25; // 0..2, broad tonal separation / local range
static const float NR_COMPARE_SPLIT       = 1.00; // left original, right processed
static const float NR_USE_FLOATZ          = 0.00; // set 1 ONLY for matching packed BO3 Float-Z
static const float NR_MATERIAL_RESPONSE   = 1.00; // 0..1, requires Structure > 0 and image evidence
static const float NR_CONTACT_STRENGTH    = 0.65; // 0..1, requires Structure > 0 and valid Float-Z
static const float NR_DENOISE             = 0.20; // 0..1, requires Structure > 0; flat regions only

struct VS_INPUT { float3 position : POSITION; float2 texcoord : TEXCOORD0; };
struct PS_INPUT { float4 position : SV_Position; float2 texcoord : TEXCOORD0; };

PS_INPUT vs_main(const VS_INPUT vertex, const uint instance : INSTANCE_SEMANTIC)
{
    PS_INPUT output;
    PostFx_GenerateFullscreenQuad(vertex.position, vertex.texcoord, instance,
                                 output.position, output.texcoord);
    return output;
}

float NR_Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float NR_Log(float3 c) { return log2(0.003 + NR_Luma(c)); }
float3 NR_Chroma(float3 c) { return c / (0.06 + NR_Luma(c)); }
float3 NR_Scene(float2 uv)
{
    return max(PostFx_NormalizeColor(frameBuffer.SampleLevel(
        frameBufferSampler, saturate(uv), 0.0).rgb), 0.0);
}

// x = decoded inverse distance (near-plane units), y = world/viewmodel class,
// z = validity. Point loads avoid mixing packed world/viewmodel codes.
// Near clip cancels from relative depth and plane-curvature tests.
float3 NR_Depth(float2 uv, float2 size)
{
    if(NR_USE_FLOATZ < 0.5) return float3(1.0, 0.0, 0.0);
    int2 p = int2(clamp(floor(saturate(uv) * size), 0.0, size - 1.0));
    float raw = DepthSampler.Load(int3(p, 0)).r;
    // Comparisons also reject NaN, infinity, sky/empty zero and white fallback.
    if(!(raw > 0.000001 && raw < 0.999999)) return float3(1.0, 0.0, 0.0);
    return float3(FloatZ_Process(raw), step(63.0 / 64.0, raw), 1.0);
}

float NR_DepthWeight(float3 a, float3 b)
{
    if(a.z < 0.5) return 1.0; // image-only fallback at invalid center
    if(b.z < 0.5 || abs(a.y - b.y) > 0.5) return 0.0;
    float difference = abs(a.x - b.x) / max(max(a.x, b.x), 0.000001);
    return 1.0 - smoothstep(0.025, 0.16, difference);
}

static const float2 NR_DIRECTIONS[8] = {
    float2(1,0), float2(0,1), float2(0.707107,0.707107),
    float2(0.707107,-0.707107), float2(0.923880,0.382683),
    float2(0.382683,0.923880), float2(0.923880,-0.382683),
    float2(0.382683,-0.923880)
};

struct NR_Scale
{
    float3 color;
    float logMean;
    float variance;
    float low;
    float high;
    float coherence;
    float contact;
    float boundary;
    float support;
    float pairedPeak;
    float pairedContrast;
};

NR_Scale NR_Gather(float2 uv, float2 px, float2 depthSize, float radius,
                  float3 center, float3 dc, float rangeWidth)
{
    NR_Scale s;
    float lc = NR_Log(center);
    float3 cc = NR_Chroma(center);
    float weight = 2.0;
    float3 sum = center * weight;
    float sumLog = lc * weight;
    float sumSquare = lc * lc * weight;
    s.low = lc; s.high = lc;
    float3 tensor = 0.0;
    float contact = 0.0, boundary = 0.0, support = 0.0, peak = 0.0;
    float pairedContrast = 0.0;
    [unroll]
    for(int i = 0; i < 8; ++i)
    {
        float2 offset = NR_DIRECTIONS[i] * radius * px;
        float3 a = NR_Scene(uv + offset);
        float3 b = NR_Scene(uv - offset);
        float3 da = NR_Depth(uv + offset, depthSize);
        float3 db = NR_Depth(uv - offset, depthSize);
        float ga = NR_DepthWeight(dc, da), gb = NR_DepthWeight(dc, db);
        float la = NR_Log(a), lb = NR_Log(b);
        float3 ca = NR_Chroma(a) - cc, cb = NR_Chroma(b) - cc;
        float wa = ga * exp2(-abs(la-lc) / rangeWidth - 2.5 * dot(ca,ca));
        float wb = gb * exp2(-abs(lb-lc) / rangeWidth - 2.5 * dot(cb,cb));
        sum += a*wa + b*wb;
        sumLog += la*wa + lb*wb;
        sumSquare += la*la*wa + lb*lb*wb;
        weight += wa + wb;
        // Local extrema are used only for texture reconstruction, never to
        // clip the later lighting response. Depth-separated samples excluded.
        if(ga > 0.5) { s.low = min(s.low,la); s.high = max(s.high,la); }
        if(gb > 0.5) { s.low = min(s.low,lb); s.high = max(s.high,lb); }
        float g = (la-lb) * 0.5;
        float2 n = NR_DIRECTIONS[i];
        tensor += float3(n.x*n.x,n.y*n.y,n.x*n.y) * g*g;
        support += wa + wb;
        // Both sides darker distinguishes compact/ridge highlights from steps.
        peak += max(min(lc-la,lc-lb),0.0) * min(ga,gb);
        // A step has a changed neighbor on only one side; texture extrema
        // have matching-sign deviations on both sides. No additional samples.
        pairedContrast += min(abs(la-lc),abs(lb-lc))
                        * step(0.0,(la-lc)*(lb-lc))*min(ga,gb);
        boundary = max(boundary, 1.0-min(ga,gb));
        // Inverse depth is affine over a projected plane. Opposite-pair
        // curvature therefore removes ordinary perspective slope. Large jumps
        // and class boundaries are rejected rather than drawn as dark outlines.
        float curvature = (da.x+db.x-2.0*dc.x) / max(dc.x,0.000001);
        float validPair = dc.z * da.z * db.z * min(ga,gb);
        contact += smoothstep(0.008,0.065,curvature) * validPair;
    }
    s.color = sum / weight;
    s.logMean = sumLog / weight;
    s.variance = max(sumSquare / weight - s.logMean*s.logMean,0.0);
    float trace = tensor.x + tensor.y;
    s.coherence = saturate(sqrt((tensor.x-tensor.y)*(tensor.x-tensor.y)
                              + 4.0*tensor.z*tensor.z) / (trace+0.0001));
    s.contact = contact * 0.125;
    s.boundary = boundary;
    s.support = saturate(support / 16.0);
    s.pairedPeak = peak * 0.125;
    s.pairedContrast = pairedContrast * 0.125;
    return s;
}

float4 ps_main(PS_INPUT input) : SV_Target
{
    float2 uv = saturate(input.texcoord);
    float4 raw = frameBuffer.SampleLevel(frameBufferSampler, uv, 0.0);
    // Return untouched engine color/alpha on bypass: no clamp or round-trip.
    if(NR_STRENGTH <= 0.0) return raw;
    float2 size = max(PostFx_GetRenderTargetSize().xy, 1.0);
    float2 px = 1.0 / size;
    if(NR_COMPARE_SPLIT > 0.5)
    {
        if(uv.x < 0.5) return raw;
        if(uv.x < 0.5 + px.x)
            return float4(PostFx_DenormalizeColor(float3(0.65,0.8,0.9)),raw.a);
    }
    float structure = clamp(NR_STRUCTURE_INTENSITY,0.0,3.0);
    float tone = clamp(NR_TONE_INTENSITY,0.0,2.0);
    if(structure <= 0.0 && tone <= 0.0) return raw;
    float3 src = max(PostFx_NormalizeColor(raw.rgb),0.0);
    float y = NR_Luma(src), lc = NR_Log(src);
    float3 dc = NR_Depth(uv,size);
    // Radius scales smoothly with resolution; no animated sampling rotation.
    float radius = clamp(size.y/1080.0,0.75,2.0);
    NR_Scale fine = NR_Gather(uv,px,size,radius,src,dc,0.65);
    NR_Scale mid = NR_Gather(uv,px,size,4.0*radius,src,dc,1.25);
    NR_Scale wide = NR_Gather(uv,px,size,14.0*radius,src,dc,1.85);

    // Nested estimates reuse every fetched sample, reducing sparse-ring bias.
    float f = fine.logMean;
    float m = lerp(f,mid.logMean,0.78);
    float base = lerp(m,wide.logMean,0.82);
    float micro = lc-f, material = f-m, localLight = m-base;
    float sigma = sqrt(fine.variance);
    float edge = smoothstep(0.35,1.15,fine.high-fine.low) * fine.coherence;
    float protection = (1.0-0.80*edge) * (1.0-fine.boundary);
    float textureEvidence = smoothstep(0.025,0.18,sigma) * (1.0-0.75*edge);
    // Relative-distance attenuation only; no fabricated world-space position.
    float nearDetail = lerp(1.0,0.65+0.35*smoothstep(0.0002,0.015,dc.x),dc.z);
    float noiseGate = (1.0-smoothstep(0.012,0.075,sigma)) * fine.support;
    // Each band gets its OWN observed-variance budget. A one-pixel extremum
    // must not clamp the 4-pixel material band: that erased broad texture peaks.
    // Soft signed limits preserve gain response without sudden hard clipping.
    float materialSigma = sqrt(mid.variance);
    float materialEvidence = smoothstep(0.015,0.12,materialSigma);
    float materialEdge = mid.coherence*smoothstep(0.75,2.0,mid.high-mid.low);
    float materialGuard = (1.0-0.85*materialEdge)*(1.0-fine.boundary);
    float stepEdge = smoothstep(0.70,1.80,mid.high-mid.low)
                   *(1.0-smoothstep(0.02,0.12,mid.pairedContrast));
    materialGuard *= 1.0-stepEdge;
    protection *= 1.0-stepEdge;
    float microDelta = 0.55*micro*textureEvidence;
    float microLimit = 0.025+0.40*sigma;
    microDelta = microDelta/(1.0+abs(microDelta)/microLimit);
    float materialDelta = 2.40*material*materialEvidence;
    float materialLimit = 0.04+0.70*materialSigma;
    materialDelta = materialDelta/(1.0+abs(materialDelta)/materialLimit);
    float detailStops = structure*nearDetail*(
        protection*(microDelta-saturate(NR_DENOISE)*noiseGate*micro)
        + materialGuard*materialDelta);
    // Global emergency bound scales with Structure; no gain plateau at 1.0.
    detailStops = clamp(detailStops,-0.65*structure,0.65*structure);
    float reconstructed = lc+detailStops;
    float newY = max(exp2(reconstructed)-0.003,0.0);
    float3 result = src * ((newY+0.00001)/(y+0.00001));

    float contact = (0.65*mid.contact+0.35*wide.contact)
                  * (1.0-fine.boundary) * (1.0-0.75*mid.boundary);
    result *= exp2(-0.40*structure*saturate(NR_CONTACT_STRENGTH)*contact);

    // Appearance evidence is deliberately not a semantic/material classifier.
    float3 chroma = NR_Chroma(src);
    float chromaRange = max(chroma.r,max(chroma.g,chroma.b))
                      - min(chroma.r,min(chroma.g,chroma.b));
    float neutral = 1.0-smoothstep(0.35,1.05,chromaRange);
    float compact = smoothstep(0.025,0.30,fine.pairedPeak+mid.pairedPeak*0.5);
    // Relative highlights matter on dim wet paving too, not just white lamps.
    float highlight = smoothstep(0.0005,0.008,y);
    float relativeHighlight = smoothstep(0.025,0.28,max(lc-m,0.0));
    float sheen = compact*(0.35+0.65*neutral)*highlight*materialGuard;
    float response = structure*saturate(NR_MATERIAL_RESPONSE);
    // Enhance observed specular/reflection ridges; never synthesize reflected
    // geometry. Warm lamps, white lettering and painted objects remain ambiguous.
    // Redistribute existing high-frequency luminance around the medium-scale
    // surface estimate. Signed response also darkens below-base surface detail;
    // a uniform region receives no sheen exposure. This is not true reflection.
    float reflectionBand = clamp(lc-m,-0.65,0.65);
    float reflectionEvidence = (0.35+0.65*neutral)*highlight
                             *materialEvidence*materialGuard;
    float reflectionStops = reflectionEvidence*reflectionBand*0.65
                          + sheen*relativeHighlight*0.20;
    result *= exp2(response*reflectionStops);
    float warm = smoothstep(0.05,0.35,chroma.r-chroma.b)
               * smoothstep(0.0,0.22,chroma.g-chroma.b)
               * (1.0-smoothstep(0.45,0.95,chroma.r-chroma.g));
    float skin = warm*smoothstep(0.025,0.10,y)*(1.0-smoothstep(0.6,1.1,y))
               * (1.0-smoothstep(0.15,0.55,sigma));
    float green = smoothstep(0.035,0.30,chroma.g-max(chroma.r,chroma.b));
    float nearbyLight = saturate((NR_Luma(mid.color)-y)/(0.08+y));
    float thin = smoothstep(0.025,0.18,max(-micro,0.0)) * fine.support;
    float transportGuard = protection*min(mid.support,wide.support);
    // Soft wavelength-biased spatial diffusion only where brighter same-region
    // neighbors exist. No constant orange skin tint or uniform foliage glow.
    float3 incoming = max(mid.color-src,0.0);
    result += incoming * float3(0.16,0.065,0.025) * skin*nearbyLight
            * response*transportGuard;
    result += incoming * float3(0.07,0.14,0.035) * green*thin
            * response*transportGuard;

    // Tone has its own low-frequency response. Previously the fine-detail
    // protection, medium coherence and support masks multiplied its already
    // small band down to nearly zero. None of those texture masks belongs on
    // the base tone curve. Structure = 0 must still leave Tone fully useful.
    // Pivoted base contrast: dim bases darken and lit bases brighten. This
    // changes tonal separation, not a uniform exposure/shadow lift. Black stays
    // black. The pivot is in normalized linear BO3 color, not display gamma.
    float baseOffset = base-log2(0.003+0.10);
    float baseStops = 0.60*baseOffset/(1.0+abs(baseOffset));
    // Compress local illumination excursions within the same surface region:
    // recover below-base signal and restrain above-base illumination, leaving
    // the fine/material bands intact. Guard actual depth breaks / hard steps.
    float lightingGuard = (1.0-mid.boundary)*(1.0-stepEdge);
    float localStops = -0.85*localLight/(1.0+abs(localLight)/0.60);
    float stops = baseStops + localStops*lightingGuard;
    result *= exp2(tone*stops);
    // Neighbor-derived indirect color changes chromaticity at fixed luminance,
    // rather than adding another brightness pedestal to every dark surface.
    float ry = NR_Luma(result);
    float wy = NR_Luma(wide.color);
    float incomingEvidence = saturate((wy-y)/(0.025+y));
    float3 bounceTarget = wide.color*(ry/max(wy,0.00001));
    result = lerp(result,bounceTarget,0.08*tone*incomingEvidence
                  *lightingGuard*wide.support*smoothstep(0.0001,0.005,wy));
    // Luminance-preserving chroma cleanup in flat regions; no global saturation.
    float3 clean = fine.color*((NR_Luma(result)+0.00001)
                              /(NR_Luma(fine.color)+0.00001));
    result = lerp(result,clean,saturate(saturate(NR_DENOISE)*noiseGate*0.12*structure));

    // Monotonic soft shoulder on maximum RGB preserves channel ratios and
    // avoids per-channel clipping/hue shifts. Identity below 0.72; asymptotic
    // toward 1 above it. Blending leaves HDR headroom when Tone is reduced.
    float peak = max(result.r,max(result.g,result.b));
    float excess = max(peak-0.72,0.0);
    float shoulder = 0.72 + excess/(1.0+excess/0.28);
    float mapped = lerp(peak,min(peak,shoulder),tone*0.50);
    result *= mapped/max(peak,0.00001);
    // No final saturate: preserve HDR and strength interpolation continuity.
    float3 outputColor = lerp(PostFx_NormalizeColor(raw.rgb),max(result,0.0),
                              saturate(NR_STRENGTH));
    return float4(PostFx_DenormalizeColor(outputColor),raw.a);
}
