
Texture2D gbuffer0 : register(t0);
Texture2D gbuffer1 : register(t1);
Texture2D gbuffer2 : register(t2);
Texture2D gbuffer3 : register(t3);
Texture2D depthTexture : register(t4);
Texture2D previewEnvironment : register(t5);
Texture2D previewMaterialAlbedoInput : register(t6);
SamplerState previewSampler : register(s0);
SamplerState previewEnvironmentSampler : register(s1);

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
    float4 previewDebugSettings;       // x = GBufferView enum, y = MaterialPreviewProfile
    float4 previewApeSettings;         // x visible-sky yaw, y max env mip, z SH9 valid, w baked-probe yaw
    float4 previewApeLightingCalibration; // x diffuse probe, y spec probe, z sun irradiance, w probe exposure
    float4 previewApeDiffuseSH[9];      // Lambert-convolved environment irradiance, Y-up SH9
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

float DecodeBo3Gloss(float packedGloss)
{
    // Stock BO3 does NOT store gloss as a simple normalized value.
    // GBuffer_PackGloss() folds the tangent-normal height term into the same
    // logarithmic scalar:
    //   packed = -log2(2^-gloss + normalHeight) / 17
    // followed by the 0.49755621 scale + 0.00146627566 offset.
    //
    // For the stock identity normal used by Geometry/lit when no normal map is
    // authored, GBuffer_DecodeNormal(...).w is exactly 1/3. The user's APE
    // capture also confirms the stock material uses Gloss Range 0..13, making
    // this inversion critical: the previous linear decode interpreted gloss 13
    // as ~0.09 and made the surface almost completely rough.
    float encoded = saturate((packedGloss - 0.00146627566) / 0.49755621);
    float combined = exp2(-17.0 * encoded);
    const float flatNormalHeight = 1.0 / 3.0;
    const float minGlossSignal = exp2(-17.0);
    float glossSignal = max(combined - flatNormalHeight, minGlossSignal);
    float glossValue = -log2(glossSignal);
    return saturate(glossValue / 17.0);
}

float Bo3GlossToRoughness(float normalizedGloss)
{
    // BO3 authors this channel as cosinePowerMap / a 0..17 gloss range.  Treating
    // 13/17 as the linear inverse of roughness made stock Geometry/lit look like
    // a broad modern-PBR mirror.  The logarithmic range is much closer to a
    // cosine-power authoring scale: convert 2^gloss to the equivalent lobe width
    // before feeding the GGX approximation used by this preview compositor.
    float glossValue = saturate(normalizedGloss) * 17.0;
    float cosinePower = exp2(glossValue);
    return clamp(sqrt(2.0 / (cosinePower + 2.0)), 0.02, 0.95);
}

float3 ApplyApeEnvironmentHandedness(float3 direction)
{
    float3 d = normalize(direction);
    // APE/BO3's asset-preview environment uses the opposite horizontal
    // handedness from the Studio camera frame. Keep this isolated to APE Match.
    int profile = (int)(previewDebugSettings.y + 0.5);
    if (profile == 0)
        d.x = -d.x;
    return d;
}

float3 RotateEnvironmentYaw(float3 direction, float yaw)
{
    float3 d = ApplyApeEnvironmentHandedness(direction);
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
    return previewEnvironment.SampleLevel(previewEnvironmentSampler, VisibleSkyDirectionToEquirect(ray), 0.35).rgb;
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

float3 ApplyApeDisplayCurve(float3 color)
{
    // Source-derived ordering: ToolsGfx's HDR path first normalizes by scene
    // exposure (HDR_ClampExposure uses gScene.invExposure) and only later runs
    // the presentation/tonemap stage.  The shipped TonemapLUT payload itself is
    // not present in the recovered source bundle, so use a neutral luminance
    // shoulder calibrated from the APE capture rather than the much contrastier
    // ACES filmic approximation used by older Studio phases.  Working in
    // luminance preserves the HDR sky's hue; the small chroma reduction matches
    // APE's visibly less saturated asset-viewer output.
    color = max(color, 0.0);
    const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
    float luminance = dot(color, lumaWeights);
    float mappedLuminance = luminance / (0.78 + luminance);
    float scale = mappedLuminance / max(luminance, 1e-6);
    float3 mapped = color * scale;
    float mappedMean = dot(mapped, lumaWeights);
    mapped = lerp(float3(mappedMean, mappedMean, mappedMean), mapped, 0.90);
    return saturate(mapped);
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
        color = ApplyApeDisplayCurve(color);
        color = LinearToDisplay(color);
        return saturate(color);
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
    float4 rt0 = gbuffer0.Sample(previewSampler, uv);
    float4 rt1 = gbuffer1.Sample(previewSampler, uv);
    float4 rt2 = gbuffer2.Sample(previewSampler, uv);
    float4 rt3 = gbuffer3.Sample(previewSampler, uv);
    float depth = depthTexture.Sample(previewSampler, uv).r;
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
        float3 debugNormal = DecodeBo3GBufferNormal(rt1);
        return float4(debugNormal * 0.5 + 0.5, 1.0);
    }
    if (debugMode == 8)
    {
        float r = saturate(rt2.x);
        return float4(r, r, r, 1.0);
    }
    if (debugMode == 9)
    {
        float g = DecodeBo3Gloss(rt1.z);
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

    float3 N = DecodeBo3GBufferNormal(rt1);
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
    float gloss = DecodeBo3Gloss(rt1.z);
    float roughness = Bo3GlossToRoughness(gloss);
    float3 F = specColor + (1.0 - specColor) * pow(1.0 - VdotH, 5.0);

    // Look Dev intentionally keeps Studio's modern GGX renderer. APE Match uses
    // the BO3 authoring semantics directly: cosinePowerMap is a 0..17 logarithmic
    // gloss scale, so Gloss 13 corresponds to an exponent of 8192. The normalized
    // Blinn/cosine-power lobe reproduces APE's tiny white sun point instead of
    // translating the legacy gloss into a GGX roughness whose safety clamp chopped
    // the peak off. CoreSunConstants also exposes sun specScale separately from
    // intensity; this keeps the specular term structurally independent of diffuse.
    float3 specular = 0.0;
    if (materialProfile == 0)
    {
        float glossValue = saturate(gloss) * 17.0;
        float cosinePower = exp2(glossValue);
        float cosineLobe = validHalfVector ? pow(max(NdotH, 1e-6), cosinePower) : 0.0;
        float normalizedLobe = ((cosinePower + 8.0) / (8.0 * 3.14159265)) * cosineLobe;
        specular = F * normalizedLobe * NdotL;
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
            // APE's ToolsGfx scene constants expose a global probe exposure and
            // average probe color, while its renderer runs a dedicated diffuse
            // probe compute stage before deferred lighting. Evaluate the local
            // HDR environment through Lambert-convolved SH9 rather than using
            // the background image itself as material lighting.
            float3 irradiance = EvaluateApeDiffuseIrradiance(N) * previewApeLightingCalibration.w;
            // The assetviewer LED was baked with four bounces and local probes.
            // A sky-only SH projection has almost no energy in directions facing
            // the preview floor, so retain a conservative global-probe floor from
            // avgCubeColor to represent that bounced/local-probe contribution.
            float3 bounceFloor = max(ambientTint, 0.0) * (3.14159265 * 0.60) *
                                 previewApeLightingCalibration.w;
            irradiance = max(irradiance, bounceFloor);
            // APE's probe structures carry avgCubeColor separately from probe
            // exposure. Use the recovered environment average as a mild chroma
            // adaptation term so blue/green HDR skies do not color-cast diffuse
            // GI as strongly as a raw lat-long sample would. Directional color
            // variation remains in the SH signal; this only normalizes the mean.
            float probeMean = dot(max(ambientTint, 0.0), float3(0.2126, 0.7152, 0.0722));
            float3 probeBalance = probeMean / max(ambientTint, float3(0.025, 0.025, 0.025));
            irradiance *= lerp(float3(1.0, 1.0, 1.0), probeBalance, 0.30);
            ambient = albedo * (irradiance / 3.14159265) *
                      previewApeLightingCalibration.x * previewAmbientShadow.x * ao;
        }
        else
        {
            float3 envDiffuse = EnvironmentDirection(N) * albedo * (previewAmbientShadow.x * 1.55) * ao;
            ambient = max(ambient, envDiffuse);
        }
    }
    float3 diffuse = albedo * (1.0 - F) * (NdotL / 3.14159265);
    float shadowTerm = lerp(1.0, smoothstep(0.0, 0.35, NdotL), previewAmbientShadow.y);
    float sunScale = materialProfile == 0 ? previewApeLightingCalibration.z : 1.0;
    float3 direct = (diffuse + specular) * previewLightColorFulbright.rgb *
                    previewLightDirIntensity.w * sunScale * shadowTerm;

    float3 envSpec = 0.0;
    if (previewAmbientShadow.w > 0.5 && previewAmbientShadow.z > 0.5)
    {
        float3 R = reflect(-V, N);
        if (materialProfile == 0)
        {
            // APE does not use the visible HDR background as a literal mirror.
            // The supplied APE capture is decisive here: stock Geometry/lit with
            // gloss 13 keeps a tiny, sharp *direct-sun* highlight, while the
            // reflection-probe contribution remains broad and low contrast.
            //
            // Keep direct gloss and probe blur as two different pieces of state.
            // A modern roughness->mip conversion coupled them too tightly and
            // made core_script_wall_c reflect recognizable clouds/terrain.
            float maxLod = max(0.0, previewApeSettings.y);
            float dielectricWeight = 1.0 - saturate((reflectance - 0.04) * 5.0);

            // Stock dielectric materials are forced into the middle/high probe
            // mips even when their authored direct-light gloss is high. Explicit
            // high-reflectance/metal-like materials are allowed to retain the
            // sharper roughness-driven lookup.
            float physicalLodFraction = saturate(sqrt(roughness));
            float apeDielectricLodFraction = saturate(0.34 + (1.0 - gloss) * 0.16);
            float lodFraction = lerp(physicalLodFraction,
                                     max(physicalLodFraction, apeDielectricLodFraction),
                                     dielectricWeight);
            float lod = lodFraction * maxLod;
            float3 env = max(EnvironmentDirectionLod(R, lod), 0.0);

            // Reflection-probe records carry exposure and avgCubeColor as separate
            // fields. avgCubeColor is metadata/fallback, not a replacement for the
            // directional probe. Phase 1k blended too heavily toward that average
            // and erased APE's broad blue-gray view-dependent response. Keep the
            // directional filtered mip dominant, only tame HDR spikes and use the
            // average as a mild low-frequency stabilization term.
            float probeLuminance = dot(env, float3(0.2126, 0.7152, 0.0722));
            float compression = rcp(1.0 + max(probeLuminance, 0.0) * 0.12);
            float3 compressedEnv = env * compression;
            float3 probeAverage = max(previewEnvironmentAmbient.rgb, 0.0);
            float3 processedEnv = lerp(compressedEnv, probeAverage, 0.16 * dielectricWeight);

            // Schlick Fresnel is intentionally left strong at grazing angles: the
            // APE recording shows a pronounced cool rim even though face-on stock
            // dielectric reflectance remains only 0.04.
            float3 fresnelEnv = specColor + (1.0 - specColor) * pow(1.0 - NdotV, 5.0);
            float stockDielectricEnergy = lerp(1.0, 0.82, dielectricWeight);
            envSpec = processedEnv * fresnelEnv * previewApeLightingCalibration.y *
                      previewApeLightingCalibration.w * stockDielectricEnergy * ao;
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
