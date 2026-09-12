
Texture2D gbuffer0 : register(t0);
Texture2D gbuffer1 : register(t1);
Texture2D gbuffer2 : register(t2);
Texture2D gbuffer3 : register(t3);
Texture2D depthTexture : register(t4);
Texture2D previewEnvironment : register(t5);
Texture2D previewMaterialAlbedoInput : register(t6);
TextureCube previewReflectionProbe : register(t7);
Texture2D<float2> previewApeEnvBrdf : register(t8);
Texture2D<float> previewApeShadowMap : register(t9);
SamplerState previewSampler : register(s0);
SamplerState previewEnvironmentSampler : register(s1);
SamplerComparisonState previewApeShadowSampler : register(s2);

cbuffer PreviewCamera : register(b0)
{
    float4 previewCameraRight;
    float4 previewCameraUp;
    float4 previewCameraForward;
    float4 previewCameraParams;
    float4 previewCameraPosition;
};
cbuffer PreviewDeferredLight : register(b13)
{
    float4 previewLightDirIntensity;
    float4 previewAmbientShadow;       // x ambient, y shadow, z env affects light, w environment enabled
    float4 previewEnvironmentAmbient;  // rgb environment ambient tint, w = forward fallback marker
    float4 previewLightColorFulbright; // rgb sun color, w fulbright
    float4 previewBackgroundColor;     // preview clear/background color
    float4 previewLookdevSettings;     // x exposure EV, y tone map, z ground, w contact shadow
    float4 previewDebugSettings;       // x = GBufferView, y = profile, z = preview mesh kind, w = native APE reference mesh
    float4 previewApeSettings;         // x visible-sky yaw, y max env mip, z SH9 valid, w baked-probe yaw
    float4 previewApeLightingCalibration; // x diffuse probe, y spec probe, z sun irradiance, w probe exposure
    float4 previewApeGlobalProbeAverage; // captured CoreSunConstants.avgGlobalProbeColor (rgb)
    float4 previewApeDiffuseSH[9];      // Lambert-convolved environment irradiance, Y-up SH9
    float4 previewApeShadowRow0;
    float4 previewApeShadowRow1;
    float4 previewApeShadowRow2;
    float4 previewApeShadowRow3;
    float4 previewApeShadowParams;      // x texel size, y depth bias, z enabled, w reserved
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
};

// BO3 does not store world normals in RT1 as normal * 0.5 + 0.5.
// GBuffer_PackNormal() projects the normal into an orthonormal tetrahedral
// basis, stores two coordinates, and puts the selected tetrahedron direction
// in NormalGloss.w. Decode that exact contract here so the lookdev viewport
// follows the same GBuffer representation as the exported BO3 material.
float3 DecodeBo3GBufferNormal(float4 normalGloss)
{
    float2 q = (normalGloss.xy - 0.5) / 0.588235;
    float r2 = saturate(dot(q, q));
    float c = max(0.0, 1.0 - r2);
    float scale = sqrt(c + 1.0);
    float a = q.x * scale;
    float b = q.y * scale;

    const float invSqrt6 = 0.4082482904638631;
    const float invSqrt2 = 0.7071067811865475;
    const float invSqrt3 = 0.5773502691896258;
    float3 n = float3(
        a * invSqrt6 - b * invSqrt2 + c * invSqrt3,
       -2.0 * a * invSqrt6              + c * invSqrt3,
        a * invSqrt6 + b * invSqrt2 + c * invSqrt3);

    int direction = (int)round(saturate(normalGloss.w) * 3.0);
    if (direction == 1)      n *= float3( 1.0, -1.0, -1.0);
    else if (direction == 2) n *= float3(-1.0,  1.0, -1.0);
    else if (direction == 3) n *= float3(-1.0, -1.0,  1.0);

    float len2 = dot(n, n);
    return len2 > 1e-8 ? n * rsqrt(len2) : float3(0.0, 0.0, 1.0);
}

// APE deferred lighting consumes the normalized PACKED NormalGloss.z signal.
// It does not run a separate "author gloss" decoder here. For the stock APE
// 8x8 normal texture (128,128,0), the height term is zero, so Gloss 13 packs to
// a normalized lighting signal of about 13/17. Non-zero blue/height data folds
// into the packed value exactly as BO3's GBuffer contract intends.
float DecodeBo3LightingGlossSignal(float packedGloss)
{
    // Captured 2f9c1c21e9bef37c sequence for the ordinary (<0.5) material path:
    //   (packedGloss - 0.00146627566) * 2.00982332
    // 2.00982332 is 1 / 0.49755621. A second packed branch exists above 0.5; keep
    // it because APE's shader does, even though core_script_wall_c stays below it.
    float base = packedGloss >= 0.5 ? 0.5 : 0.00146627566;
    return saturate((packedGloss - base) * 2.00982332);
}

float DecodeBo3AuthoredGloss(float packedGloss)
{
    // The GBuffer does not retain enough information to uniquely separate
    // author gloss from a non-zero normal-height fold. APE's stock neutral
    // normal has blue/height = 0, where the normalized packed signal is also
    // the author-facing 0..17 gloss fraction, so expose that same signal here.
    return DecodeBo3LightingGlossSignal(packedGloss);
}

float Bo3LightingGlossToAlpha(float lightingGlossSignal)
{
    // Exact captured APE width conversion:
    //   cosinePower = 2^(17 * lightingGlossSignal)
    //   alpha^2     = 2 / (cosinePower + 2)
    float cosinePower = exp2(17.0 * saturate(lightingGlossSignal));
    return sqrt(max(2.0 / (cosinePower + 2.0), 1e-10));
}

float3 StudioToApeEnvironmentFrame(float3 direction)
{
    float3 d = normalize(direction);
    int profile = (int)(previewDebugSettings.y + 0.5);
    if (profile == 0)
    {
        // Phase 1z: Phase 1v recovered the full APE -> Studio world transform:
        //   Studio X = -APE Y, Studio Y = APE Z, Studio Z = APE X.
        // The environment path was still using the much older X-only flip, so
        // sky/probe directions lived in a different frame from N/L/V. Convert
        // Studio world back into APE's authored Z-up frame, then express it as
        // the Y-up lat-long/cubemap sampling frame used by the preview textures:
        //   APE = (StudioZ, -StudioX, StudioY)
        //   sampleFrame = (APE X, APE Z, APE Y)
        //               = (StudioZ, StudioY, -StudioX).
        d = float3(d.z, d.y, -d.x);
    }
    return normalize(d);
}

float3 RotateEnvironmentYaw(float3 direction, float yaw)
{
    float3 d = StudioToApeEnvironmentFrame(direction);
    float sy = sin(yaw), cy = cos(yaw);
    d.xz = float2(d.x * cy - d.z * sy, d.x * sy + d.z * cy);
    return normalize(d);
}

float3 RotateVisibleSkyDirection(float3 direction)
{
    // Horizontal light manipulation yaws APE's visible sky. Vertical light
    // manipulation never pitches/rolls it.
    return RotateEnvironmentYaw(direction, previewApeSettings.x);
}

float3 RotateBakedProbeDirection(float3 direction)
{
    // APE's glossy/diffuse probe is baked at the preset orientation. It does
    // not follow manual light yaw or pitch, even though the visible sky yaws.
    return RotateEnvironmentYaw(direction, previewApeSettings.w);
}

float2 DirectionToEquirectFromDirection(float3 d)
{
    float u = atan2(d.z, d.x) * 0.15915494309189535 + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) * 0.3183098861837907;
    return float2(frac(u), saturate(v));
}

float2 VisibleSkyDirectionToEquirect(float3 direction)
{
    return DirectionToEquirectFromDirection(RotateVisibleSkyDirection(direction));
}

float2 BakedProbeDirectionToEquirect(float3 direction)
{
    return DirectionToEquirectFromDirection(RotateBakedProbeDirection(direction));
}

float3 EnvironmentAt(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 ray = previewCameraForward.xyz +
                 previewCameraRight.xyz * (ndc.x * previewCameraParams.x * previewCameraParams.y) +
                 previewCameraUp.xyz * (ndc.y * previewCameraParams.y);
    return previewEnvironment.SampleLevel(previewEnvironmentSampler, VisibleSkyDirectionToEquirect(ray), 0.0).rgb;
}

float LinearToDisplay1(float x)
{
    x = max(x, 0.0);
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 LinearToDisplay(float3 color)
{
    return float3(LinearToDisplay1(color.r), LinearToDisplay1(color.g), LinearToDisplay1(color.b));
}

float3 ApeRec709ToLinear(float3 c)
{
    float3 low = c / 4.5;
    float3 high = pow(max((c + 0.099) / 1.099, 0.0), 1.0 / 0.45);
    return float3(c.r <= 0.081 ? low.r : high.r,
                  c.g <= 0.081 ? low.g : high.g,
                  c.b <= 0.081 ? low.b : high.b);
}

float3 ApplyApeDisplayCurve(float3 color)
{
    // Phase 1o is capture-derived from APE's actual pixel shader
    // 275dce0f2b3a7c36. The default material viewport has its LUT branch
    // disabled, so the filmic polynomial below is the real presentation curve
    // used by the supplied APE Day captures.
    color = max(color, 0.0);
    float3 x = saturate(log2(color + 0.008730) * 0.0727029592 + 0.598206);
    float3 x2 = x * x;
    float3 x3 = x2 * x;
    float3 x4 = x3 * x;
    float3 x5 = x4 * x;
    float3 mapped = saturate(
          7.712947    * x5
        - 19.311527   * x4
        + 14.275167   * x3
        - 2.49004531  * x2
        + 0.878083050 * x
        - 0.0669102818);

    // APE then decodes the polynomial result from Rec.709 transfer space to
    // linear and encodes that linear result for the desktop sRGB target.
    return LinearToDisplay(ApeRec709ToLinear(mapped));
}

float3 ApplyLookdev(float3 color)
{
    int profile = (int)(previewDebugSettings.y + 0.5);
    color = max(color, 0.0);

    // APE's No Lighting path remains a distinct diagnostic renderer mode.
    if (profile == 2)
        return saturate(LinearToDisplay(color));

    color *= exp2(previewLookdevSettings.x);
    int mode = (int)(previewLookdevSettings.y + 0.5);
    if (profile == 0)
    {
        // ApplyApeDisplayCurve already performs APE's final Rec.709 decode and
        // desktop sRGB encoding. Do not encode it a second time.
        return saturate(ApplyApeDisplayCurve(color));
    }

    if (mode == 1)
        color = color / (1.0 + color); // Reinhard
    else if (mode == 2)
    {
        const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));
    }
    return saturate(color);
}

float3 EnvironmentDirection(float3 direction)
{
    return previewEnvironment.SampleLevel(previewEnvironmentSampler, BakedProbeDirectionToEquirect(direction), 0.0).rgb;
}

float3 EnvironmentDirectionLod(float3 direction, float lod)
{
    return previewEnvironment.SampleLevel(previewEnvironmentSampler, BakedProbeDirectionToEquirect(direction),
        clamp(lod, 0.0, max(0.0, previewApeSettings.y))).rgb;
}

float3 ReflectionProbeDirectionLod(float3 direction, float lod)
{
    // Phase 1p uses a real 256x256 TextureCube with seven authored/prefiltered
    // mips, matching the resource type captured from APE at t51. The probe stays
    // baked at the preset orientation while the visible lat-long sky can yaw.
    float3 d = RotateBakedProbeDirection(direction);
    return previewReflectionProbe.SampleLevel(previewEnvironmentSampler, d,
        clamp(lod, 0.0, max(0.0, previewApeSettings.y))).rgb;
}

float3 RecoverApeProbeDirectionalContrast(float3 probeSample, float ndotv)
{
    // Phase 1ab: after 1z aligned the reflection pattern, the Reset A/B pair
    // gives a clean amplitude measurement: Studio retained ~25.7 vs APE ~32.1
    // luminance sigma through the mid/outer sphere, and ~35.0 vs ~47.8 in the
    // outer grazing annulus. Preserve mean probe energy and restore only the
    // missing directional range. The stronger grazing correction follows the
    // measured radial error instead of globally boosting reflections.
    float grazing = 1.0 - saturate(ndotv);
    float apeDirectionalContrast = lerp(2.18, 2.38, grazing * grazing);
    float3 probeMean = max(previewEnvironmentAmbient.rgb, 0.0);
    return max(probeMean + (probeSample - probeMean) * apeDirectionalContrast, 0.0);
}

float2 SampleApeEnvBrdf(float ndotv, float lightingGlossSignal)
{
    // Captured shader 2f9c1c21e9bef37c samples gEnvBRDFGeneric with
    // (NdotV, normalized packed-gloss signal), NOT microfacet alpha/roughness.
    // For the stock wall this coordinate is ~13/17; it is not microfacet alpha.
    float2 uv = saturate(float2(ndotv, lightingGlossSignal));
    uv = uv * (63.0 / 64.0) + (0.5 / 64.0);
    return previewApeEnvBrdf.SampleLevel(previewSampler, uv, 0.0).rg;
}

float3 ReconstructPreviewWorldPosition(float2 uv, float depth, float3 viewRay)
{
    // Studio's APE material camera uses the same fixed 0.05..100 perspective
    // range for the GBuffer. Reconstruct world position so the capture-derived
    // sun-shadow pass can use a real shadow comparison instead of NdotL shading.
    const float nearZ = 0.05;
    const float farZ = 100.0;
    const float A = farZ / (farZ - nearZ);
    const float B = nearZ * farZ / (farZ - nearZ);
    float viewZ = B / max(A - depth, 1e-6);
    float forwardDot = max(dot(viewRay, normalize(previewCameraForward.xyz)), 1e-5);
    float rayDistance = viewZ / forwardDot;
    return previewCameraPosition.xyz + viewRay * rayDistance;
}

// Phase 1x: APE's stock preview sphere is a true geometric sphere and the
// captured decoded NormalGloss field agrees with the analytic radial normal to
// essentially machine precision across the visible face.  Reconstruct that same
// outward normal for APE Match's Sphere preview. This removes raster winding and
// SV_IsFrontFace from the reference-lighting equation without altering exported
// BO3 material behavior or non-sphere preview modes.
float3 ResolveApePreviewNormal(float4 normalGloss, float2 uv, float depth, float3 viewRay)
{
    float3 decodedNormal = DecodeBo3GBufferNormal(normalGloss);
    const int profile = (int)(previewDebugSettings.y + 0.5);
    const int meshKind = (int)(previewDebugSettings.z + 0.5);
    if (profile == 0 && meshKind == 1)
    {
        float3 worldPosition = ReconstructPreviewWorldPosition(uv, depth, viewRay);
        float radiusSq = dot(worldPosition, worldPosition);
        if (radiusSq > 1e-8)
            return worldPosition * rsqrt(radiusSq);
    }
    return decodedNormal;
}

float SampleApeSunShadow(float3 worldPosition, float3 surfaceNormal, float ndotl)
{
    if (previewApeShadowParams.z < 0.5)
        return 1.0;

    // Phase 1r: the old Phase 1o/q shadow target was a color R16 copy of
    // SV_Position.z with no raster depth bias. Reprojecting the camera depth
    // back to world space then comparing against that copy produced severe
    // self-shadow acne, which erased almost the entire APE direct-light
    // hemisphere and left only a thin moving strip. APE's real map is a
    // hardware depth surface. Offset the receiver along the geometric normal
    // (slope-scaled near the terminator) before the comparison, matching the
    // role of the native shadow raster bias without weakening broad N.L light.
    float normalBias = previewApeShadowParams.w * lerp(2.0, 1.0, saturate(ndotl));
    worldPosition += normalize(surfaceNormal) * normalBias;

    float4 p = float4(worldPosition, 1.0);
    float4 clip = float4(
        dot(previewApeShadowRow0, p),
        dot(previewApeShadowRow1, p),
        dot(previewApeShadowRow2, p),
        dot(previewApeShadowRow3, p));
    if (abs(clip.w) < 1e-6)
        return 1.0;
    float3 ndc = clip.xyz / clip.w;
    float2 suv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(suv < 0.0) || any(suv > 1.0) || ndc.z <= 0.0 || ndc.z >= 1.0)
        return 1.0;

    const float2 taps[8] = {
        float2(-0.80811429,  0.80811429),
        float2( 0.00000000, -1.00000000),
        float2( 0.60605717,  0.60605717),
        float2(-0.71428573,  0.00000000),
        float2( 0.40411428, -0.40411428),
        float2( 0.00000000,  0.42857143),
        float2(-0.20205714, -0.20205714),
        float2( 0.14285715,  0.00000000)
    };
    float sum = 0.0;
    [unroll] for (int tap = 0; tap < 8; ++tap)
    {
        float2 offset = taps[tap] * previewApeShadowParams.x;
        sum += previewApeShadowMap.SampleCmpLevelZero(
            previewApeShadowSampler, suv + offset, ndc.z - previewApeShadowParams.y);
    }
    float filtered = sum * 0.125;
    // The captured deferred shader cubes the 8-tap comparison average.
    return filtered * filtered * filtered;
}

float3 EvaluateApeDiffuseIrradiance(float3 direction)
{
    if (previewApeSettings.z < 0.5)
        return max(previewEnvironmentAmbient.rgb * 3.14159265, 0.0);

    float3 d = RotateBakedProbeDirection(direction);
    float x = d.x, y = d.y, z = d.z;
    float basis[9] = {
        0.2820947918,
        0.4886025119 * z,
        0.4886025119 * y,
        0.4886025119 * x,
        1.0925484306 * x * z,
        1.0925484306 * z * y,
        0.3153915653 * (3.0 * y * y - 1.0),
        1.0925484306 * x * y,
        0.5462742153 * (x * x - z * z)
    };
    float3 irradiance = 0.0;
    [unroll] for (int sh = 0; sh < 9; ++sh)
        irradiance += previewApeDiffuseSH[sh].rgb * basis[sh];
    return max(irradiance, 0.0);
}

float4 ps_main(VS_OUT i) : SV_Target0
{
    float2 uv = i.texcoord0.xy;

    // Captured APE deferred shader 2f9c1c21e9bef37c reads its GBuffer and
    // depth with integer ld instructions. These surfaces contain packed data
    // (especially NormalGloss), so bilinear filtering changes BRDF inputs at
    // silhouettes and is not instruction-faithful. SV_Position is already in
    // pixel coordinates; truncation maps the pixel centre to the same texel.
    int2 pixelCoord = int2(i.position.xy);
    float4 rt0 = gbuffer0.Load(int3(pixelCoord, 0));
    float4 rt1 = gbuffer1.Load(int3(pixelCoord, 0));
    float4 rt2 = gbuffer2.Load(int3(pixelCoord, 0));
    float4 rt3 = gbuffer3.Load(int3(pixelCoord, 0));
    float depth = depthTexture.Load(int3(pixelCoord, 0)).r;
    int debugMode = (int)(previewDebugSettings.x + 0.5);

    // Raw MRT/depth inspector modes are intentionally unprocessed.
    if (debugMode == 1) return float4(rt0.rgb, 1.0); // RT0
    if (debugMode == 2) return float4(rt1.rgb, 1.0); // RT1
    if (debugMode == 3) return float4(rt2.rgb, 1.0); // RT2
    if (debugMode == 4) return float4(rt3.rgb, 1.0); // RT3
    if (debugMode == 5)
    {
        float d = saturate(depth);
        float v = 1.0 - pow(d, 0.22);
        return float4(v, v, v, 1.0);
    }
    if (debugMode == 12)
    {
        // Direct resource diagnostic: bypass mesh UVs and the GBuffer entirely.
        // The albedo SRV is sRGB-aware, so encode its linear sample back to the
        // UNORM desktop target for an apples-to-apples view of the source image.
        return float4(saturate(LinearToDisplay(previewMaterialAlbedoInput.Sample(previewSampler, uv).rgb)), 1.0);
    }

    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 viewRay = normalize(previewCameraForward.xyz +
        previewCameraRight.xyz * (ndc.x * previewCameraParams.x * previewCameraParams.y) +
        previewCameraUp.xyz * (ndc.y * previewCameraParams.y));

    if (depth >= 0.99999)
    {
        const int profile = (int)(previewDebugSettings.y + 0.5);
        if (profile == 2)
            return float4(previewBackgroundColor.rgb, 1.0);

        // Procedural lookdev floor. It is a viewport aid only and never changes
        // the BO3 shader/package being validated.
        if (previewLookdevSettings.z > 0.5 && viewRay.y < -0.0001)
        {
            float t = (-1.05 - previewCameraPosition.y) / viewRay.y;
            if (t > 0.0)
            {
                float3 p = previewCameraPosition.xyz + viewRay * t;
                if (max(abs(p.x), abs(p.z)) < 7.0)
                {
                    float2 cell = floor(p.xz * 2.0);
                    float checker = fmod(abs(cell.x + cell.y), 2.0);
                    float3 floorColor = lerp(float3(0.075,0.078,0.082), float3(0.105,0.108,0.114), checker);
                    float contact = exp(-dot(p.xz, p.xz) * 2.8) * previewLookdevSettings.w;
                    float ndotl = saturate(previewLightDirIntensity.y);
                    float3 floorEnv = (previewAmbientShadow.w > 0.5) ? EnvironmentDirection(float3(0.0, 1.0, 0.0)) : previewEnvironmentAmbient.rgb;
                    float3 floorLight = floorEnv * (previewAmbientShadow.x * 1.8) + previewLightColorFulbright.rgb * (ndotl * previewLightDirIntensity.w * 0.65);
                    floorColor *= max(float3(0.10, 0.10, 0.10), floorLight) * (1.0 - contact * 0.72);
                    return float4(ApplyLookdev(floorColor), 1.0);
                }
            }
        }
        if (previewAmbientShadow.w > 0.5)
            return float4(ApplyLookdev(EnvironmentAt(uv)), 1.0);
        return float4(ApplyLookdev(previewBackgroundColor.rgb), 1.0);
    }

    float3 albedo = max(rt0.rgb, 0.0);
    float3 emissive = max(rt3.rgb, 0.0); // preview-only fallback; BO3 opaque GBuffer has no emissive MRT
    const int materialProfile = (int)(previewDebugSettings.y + 0.5);

    // Semantic inspector views decode the actual BO3 GBuffer contract instead
    // of displaying packed channels as though they were ordinary RGB textures.
    if (debugMode == 6)
    {
        // RT0 stores linear albedo. APE Match/Neutral are presented through an
        // UNORM desktop swapchain, so encode for display when inspecting the
        // semantic albedo channel.
        const bool apeDisplay = materialProfile == 0 || materialProfile == 2;
        return float4(saturate(apeDisplay ? LinearToDisplay(albedo) : albedo), 1.0);
    }
    if (debugMode == 7)
    {
        float3 debugNormal = ResolveApePreviewNormal(rt1, uv, depth, viewRay);
        return float4(debugNormal * 0.5 + 0.5, 1.0);
    }
    if (debugMode == 8)
    {
        float r = saturate(rt2.x);
        return float4(r, r, r, 1.0);
    }
    if (debugMode == 9)
    {
        float g = DecodeBo3AuthoredGloss(rt1.z);
        return float4(g, g, g, 1.0);
    }
    if (debugMode == 10)
    {
        float a = saturate(rt2.z);
        return float4(a, a, a, 1.0);
    }
    if (debugMode == 11) return float4(max(rt3.rgb, 0.0), 1.0);

    if (previewLightColorFulbright.w > 0.5 || materialProfile == 2)
        return float4(ApplyLookdev(albedo + emissive), 1.0);

    float3 N = ResolveApePreviewNormal(rt1, uv, depth, viewRay);
    float3 L = normalize(previewLightDirIntensity.xyz);
    float3 V = normalize(-viewRay);

    // L + V becomes zero when the light is directly behind the viewer. Guard
    // that mathematical singularity for robustness. The dark moving APE patch in
    // the user's reference captures is a real APE viewport behavior, not evidence
    // of this half-vector case, so do not use this guard to erase that reference
    // shading behavior.
    float3 halfVectorRaw = L + V;
    float halfVectorLengthSq = dot(halfVectorRaw, halfVectorRaw);
    bool validHalfVector = halfVectorLengthSq > 1e-8;
    float3 H = validHalfVector ? halfVectorRaw * rsqrt(halfVectorLengthSq) : N;

    float NdotL = saturate(dot(N,L));
    float NdotV = saturate(dot(N,V));
    float NdotH = validHalfVector ? saturate(dot(N,H)) : 0.0;
    float VdotH = validHalfVector ? saturate(dot(V,H)) : 0.0;

    // BO3 GBuffer RT2 is ReflectanceOcclusion, not a conventional
    // (specular.rgb, gloss) texture. Generated procedural materials currently
    // use the stock dielectric 0.04 reflectance; RT2.z is occlusion. RT1.z owns
    // the packed gloss value.
    float reflectance = saturate(rt2.x);
    float3 specColor = max(float3(reflectance, reflectance, reflectance), float3(0.04, 0.04, 0.04));
    float lightingGloss = DecodeBo3LightingGlossSignal(rt1.z);
    float roughness = Bo3LightingGlossToAlpha(lightingGloss);
    float3 F = specColor + (1.0 - specColor) * pow(1.0 - VdotH, 5.0);

    // Look Dev intentionally keeps Studio's artist-friendly GGX renderer. APE
    // Match follows the captured BO3 deferred path: the 0..17 cosinePowerMap is
    // converted to the microfacet alpha used by Treyarch's lighting shader.
    // CoreSunConstants also exposes sun specScale separately from intensity, so
    // direct specular remains structurally independent of diffuse irradiance.
    float3 specular = 0.0;
    if (materialProfile == 0)
    {
        // Phase 1w preserves Phase 1v's instruction-faithful direct-sun specular from
        // captured ToolsGfx/deferred_lighting.hlsl (2f9c1c21e9bef37c).
        //
        // The older Studio rewrite used textbook GGX normalization. The capture:
        //   alpha2 = 2 / (2^(17*gloss) + 2)
        //   alpha  = sqrt(alpha2)
        //   k      = (sqrt(alpha) + 1)^2 / 8
        //   Dden   = 1 + abs(N.H)^2 * (alpha2 - 1)       // NO /PI
        // and evaluates visibility as
        // (N.V*(1-k)+k) * (N.L*(1-k)+k).
        //
        // Phase 1u misread r2.w at instruction 2398. r2.w is NdotL, while r4.w
        // is the shadow factor. The captured numerator therefore DOES contain
        // NdotL: alpha2 * specScale * NdotL. Shadow is multiplied later.
        // CoreSunConstants.specScale is 1.0 in the captured Day viewport.
        const float apeSunSpecScale = 1.0;
        float alpha = roughness;
        float alpha2 = max(alpha * alpha, 1e-10);
        float sqrtAlpha = sqrt(max(alpha, 0.0));
        float visibilityK = (sqrtAlpha * 0.5 + 0.5);
        visibilityK = visibilityK * visibilityK * 0.5;
        float oneMinusK = 1.0 - visibilityK;
        float visV = NdotV * oneMinusK + visibilityK;
        float visL = NdotL * oneMinusK + visibilityK;
        float rawNdotH = validHalfVector ? dot(N, H) : 0.0;
        float dDenom = 1.0 + abs(rawNdotH) * abs(rawNdotH) * (alpha2 - 1.0);
        float specNoFresnel = 0.0;
        if (validHalfVector && NdotL > 0.0)
        {
            // Captured instruction stream (2397-2439):
            //   r3.w = alpha2 * specScale
            //   r3.w = NdotL * r3.w
            //   ...
            //   r2.w = r3.w / (visV * visL * Dden^2)
            //   r2.w *= shadow
            //   r2.w *= 0.25
            // Keep shadow outside this BRDF scalar because Phase 1s intentionally
            // holds shadow at 1 until the missing t40 shadow-tree data is recovered.
            specNoFresnel = (alpha2 * apeSunSpecScale * NdotL) /
                max(4.0 * visV * visL * dDenom * dDenom, 1e-10);
        }
        // The shader stores the base and grazing branches separately and later
        // recombines them as F0*base + (1-F0)*(1-V.H)^5. This Schlick form is
        // algebraically identical, so keep it explicit here while preserving
        // the captured lobe/visibility normalization above.
        specular = specNoFresnel * F;
    }
    else
    {
        float alpha = roughness * roughness;
        float a2 = alpha * alpha;
        float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
        float D = a2 / max(3.14159265 * denom * denom, 1e-5);
        float k = (roughness + 1.0); k = (k*k) * 0.125;
        float Gv = NdotV / max(NdotV * (1.0-k) + k, 1e-5);
        float Gl = NdotL / max(NdotL * (1.0-k) + k, 1e-5);
        specular = (D * Gv * Gl * F) / max(4.0 * NdotV * NdotL, 1e-4);
    }

    float ao = saturate(max(rt2.z, 0.15));
    float3 ambientTint = previewEnvironmentAmbient.rgb;
    float3 ambient = albedo * ambientTint * (previewAmbientShadow.x * 1.35) * ao;
    if (previewAmbientShadow.w > 0.5 && previewAmbientShadow.z > 0.5)
    {
        if (materialProfile == 0)
        {
            // Capture-grounded energy domain. In the Day capture:
            //   globalProbeExposure * invExposure = 1941.25403 / 7765.01172 = 0.25
            // and avgGlobalProbeColor is stored separately in CoreSunConstants.
            // Earlier phases multiplied raw sky SH by ~1.8 and invented a 60%
            // bounce floor, making indirect light dominate the sphere so strongly
            // that moving the sun was almost invisible. Keep the directional SH,
            // but scale it in APE's captured probe-exposure domain and use the
            // captured average probe only as the physically appropriate constant
            // radiance floor (constant radiance -> PI * L irradiance).
            float probeExposure = previewApeLightingCalibration.w;
            float3 irradiance = EvaluateApeDiffuseIrradiance(N) * probeExposure;
            float3 capturedAverageRadiance = max(previewApeGlobalProbeAverage.rgb, 0.0) * probeExposure;
            float3 bounceFloor = capturedAverageRadiance * 3.14159265;
            irradiance = max(irradiance, bounceFloor);
            ambient = albedo * (irradiance / 3.14159265) *
                      previewApeLightingCalibration.x * previewAmbientShadow.x * ao;
        }
        else
        {
            float3 envDiffuse = EnvironmentDirection(N) * albedo * (previewAmbientShadow.x * 1.55) * ao;
            ambient = max(ambient, envDiffuse);
        }
    }
    float3 diffuse = 0.0;
    float shadowTerm = 1.0;
    if (materialProfile == 0)
    {
        // Phase 1s: this term is capture-derived rather than a conventional
        // Lambert rewrite. Comparing the horizontal and vertical APE captures
        // at pixels where one capture has N.L == 0 gives:
        //
        //   litHorizontal - unlitVertical
        //     ~= linearAlbedo * (sunColor * invExposure) * N.L
        //
        // to within only a few percent across the sphere. There is NO / PI
        // here and no (1-F) multiplier on APE's diffuse sun accumulator. The
        // old Studio equation was therefore roughly 3.2x too weak before any
        // shadowing was even applied. Keep the direct specular term separate.
        // Phase 1t also ports the small rough-diffuse correction immediately
        // preceding the captured direct-specular block. For Gloss 13 alpha is
        // only ~0.0156, so Phase 1s's plain N.L was already close; retaining the
        // exact term improves the terminator without changing the recovered
        // direct-sun energy domain.
        float diffuseViewTerm = 1.0 - 0.5 * NdotV;
        float diffuseRetro = 1.0 - NdotL * diffuseViewTerm;
        diffuseRetro *= diffuseRetro;
        diffuseRetro = 0.62 * (1.0 - diffuseRetro) - NdotL;
        float apeDiffuseLobe = max(NdotL + roughness * diffuseRetro, 0.0);
        diffuse = albedo * apeDiffuseLobe;

        // We captured APE's three-layer gSunShadowmapArray, but not the
        // gSunShadowTree structured buffer (t40) that selects/maps those layers.
        // The single guessed Studio shadow camera was able to erase the entire
        // direct hemisphere. Until t40 is reconstructed, an unshadowed captured
        // direct-sun baseline is more faithful than a fabricated all-dark result.
        shadowTerm = 1.0;
    }
    else
    {
        diffuse = albedo * (1.0 - F) * (NdotL / 3.14159265);
        shadowTerm = lerp(1.0, smoothstep(0.0, 0.35, NdotL), previewAmbientShadow.y);
    }
    float sunScale = materialProfile == 0 ? previewApeLightingCalibration.z : 1.0;
    float3 direct = (diffuse + specular) * previewLightColorFulbright.rgb *
                    previewLightDirIntensity.w * sunScale * shadowTerm;

    float3 envSpec = 0.0;
    if (previewAmbientShadow.w > 0.5 && previewAmbientShadow.z > 0.5)
    {
        float3 R = reflect(-V, N);
        if (materialProfile == 0)
        {
            // Captured APE deferred shader uses the normalized PACKED gloss signal.
            // With APE's stock neutral normal texture (blue/height = 0), Gloss 13
            // yields about 13/17 and therefore probe LOD ~= 1.176. The important
            // Phase 1q keeps the captured 5*(1-gloss) lookup, but the cube mips
            // are now a true roughness-prefilter chain. Phase 1p accidentally
            // reused the direct 2^(17*gloss) lobe for mip convolution, leaving
            // mip 1 effectively sharp and making the result look unchanged.
            float lod = 5.0 * (1.0 - saturate(lightingGloss));
            float3 env = max(ReflectionProbeDirectionLod(R, lod), 0.0);
            env = RecoverApeProbeDirectionalContrast(env, NdotV);
            float2 dfg = SampleApeEnvBrdf(NdotV, lightingGloss);
            // Captured final material combine is 0.96 * branchA + 0.04 * branchB
            // for the stock dielectric (see the final 2f9c... instruction block).
            // Phase 1o had these terms reversed as F0*A + B, which drove the LUT
            // contribution close to 1.0 and made the wall look like a mirror.
            float3 splitSum = (1.0 - specColor) * dfg.x + specColor * dfg.y;
            envSpec = env * splitSum * previewApeLightingCalibration.y *
                      previewApeLightingCalibration.w * ao;
        }
        else
        {
            float3 env = EnvironmentDirection(R);
            envSpec = env * F * lerp(0.9, 0.18, roughness) * ao;
        }
    }

    float3 color = ambient + direct + envSpec + emissive;
    return float4(ApplyLookdev(color), 1.0);
}
