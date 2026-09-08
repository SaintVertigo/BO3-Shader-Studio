#include "beginner_shader_builder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace beginner
{
namespace
{

ParameterDefinition FloatParam(const char* key, const char* name, const char* description,
                               double minimum, double maximum, double step, double defaultValue)
{
    ParameterDefinition parameter;
    parameter.key = QString::fromLatin1(key);
    parameter.name = QString::fromLatin1(name);
    parameter.description = QString::fromLatin1(description);
    parameter.kind = ParameterKind::Float;
    parameter.minimum = minimum;
    parameter.maximum = maximum;
    parameter.step = step;
    parameter.defaultValue = defaultValue;
    return parameter;
}

ParameterDefinition ColorParam(const char* key, const char* name, const char* description, const char* color)
{
    ParameterDefinition parameter;
    parameter.key = QString::fromLatin1(key);
    parameter.name = QString::fromLatin1(name);
    parameter.description = QString::fromLatin1(description);
    parameter.kind = ParameterKind::Color;
    parameter.defaultColor = QColor(QString::fromLatin1(color));
    return parameter;
}

EffectDefinition EffectDef(const char* id, const char* name, const char* description, const char* category,
                           std::initializer_list<Target> targets,
                           std::initializer_list<ParameterDefinition> parameters)
{
    EffectDefinition definition;
    definition.id = QString::fromLatin1(id);
    definition.name = QString::fromLatin1(name);
    definition.description = QString::fromLatin1(description);
    definition.category = QString::fromLatin1(category);
    for(Target target : targets) definition.targets.push_back(target);
    for(const ParameterDefinition& parameter : parameters) definition.parameters.push_back(parameter);
    return definition;
}

QString floatLiteral(double value)
{
    if(!std::isfinite(value)) value = 0.0;
    QString text = QString::number(value, 'f', 6);
    while(text.contains('.') && text.endsWith('0')) text.chop(1);
    if(text.endsWith('.')) text += '0';
    if(!text.contains('.')) text += ".0";
    return text;
}

QColor parameterColor(const Effect& effect, const EffectDefinition& definition, const QString& key)
{
    QColor fallback(Qt::white);
    for(const ParameterDefinition& parameter : definition.parameters)
    {
        if(parameter.key == key)
        {
            fallback = parameter.defaultColor;
            break;
        }
    }
    const QColor value(effect.parameters.value(key).toString());
    return value.isValid() ? value : fallback;
}

double parameterFloat(const Effect& effect, const EffectDefinition& definition, const QString& key)
{
    for(const ParameterDefinition& parameter : definition.parameters)
    {
        if(parameter.key == key)
        {
            const double raw = effect.parameters.value(key).toDouble(parameter.defaultValue);
            return std::clamp(raw, parameter.minimum, parameter.maximum);
        }
    }
    return 0.0;
}

QString colorLiteral(const QColor& color)
{
    const QColor valid = color.isValid() ? color : QColor(Qt::white);
    return QString("float3(%1, %2, %3)")
        .arg(floatLiteral(valid.redF()), floatLiteral(valid.greenF()), floatLiteral(valid.blueF()));
}

QColor settingColor(const Project& project, const QString& key, const QColor& fallback)
{
    const QColor value(project.settings.value(key).toString());
    return value.isValid() ? value : fallback;
}

QString commonEffectCode(const Project& project, bool hasTime, bool hasUv)
{
    QString out;
    int effectIndex = 0;
    for(const Effect& effect : project.effects)
    {
        if(!effect.enabled) continue;
        const EffectDefinition* definition = effectDefinition(effect.typeId);
        if(!definition || !supportsTarget(*definition, project.target)) continue;
        const QString tag = QString("e%1").arg(effectIndex++);

        if(effect.typeId == "tint")
        {
            const QColor tint = parameterColor(effect, *definition, "color");
            const double amount = parameterFloat(effect, *definition, "amount");
            out += QString("    // %1\n    color = lerp(color, color * %2, %3);\n")
                .arg(definition->name, colorLiteral(tint), floatLiteral(amount));
        }
        else if(effect.typeId == "brightness")
        {
            out += QString("    // %1\n    color += %2;\n")
                .arg(definition->name, floatLiteral(parameterFloat(effect, *definition, "amount")));
        }
        else if(effect.typeId == "contrast")
        {
            out += QString("    // %1\n    color = (color - 0.5) * %2 + 0.5;\n")
                .arg(definition->name, floatLiteral(parameterFloat(effect, *definition, "amount")));
        }
        else if(effect.typeId == "saturation")
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            out += QString("    // %1\n    float %2_luma = dot(color, float3(0.2126, 0.7152, 0.0722));\n"
                           "    color = lerp(%2_luma.xxx, color, %3);\n")
                .arg(definition->name, tag, amount);
        }
        else if(effect.typeId == "grayscale")
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            out += QString("    // %1\n    float %2_gray = dot(color, float3(0.2126, 0.7152, 0.0722));\n"
                           "    color = lerp(color, %2_gray.xxx, %3);\n")
                .arg(definition->name, tag, amount);
        }
        else if(effect.typeId == "invert")
        {
            out += QString("    // %1\n    color = lerp(color, 1.0 - color, %2);\n")
                .arg(definition->name, floatLiteral(parameterFloat(effect, *definition, "amount")));
        }
        else if(effect.typeId == "vignette" && hasUv)
        {
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString softness = floatLiteral(parameterFloat(effect, *definition, "softness"));
            out += QString("    // %1\n    float2 %2_vp = uv * 2.0 - 1.0;\n"
                           "    float %2_v = 1.0 - smoothstep(%3, %4, dot(%2_vp, %2_vp));\n"
                           "    color *= lerp(1.0, %2_v, %5);\n")
                .arg(definition->name, tag,
                     floatLiteral(std::max(0.0, 1.0 - parameterFloat(effect, *definition, "size"))),
                     floatLiteral(std::max(0.01, 1.0 - parameterFloat(effect, *definition, "size") + parameterFloat(effect, *definition, "softness"))),
                     strength);
            Q_UNUSED(softness);
        }
        else if(effect.typeId == "scanlines" && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString density = floatLiteral(parameterFloat(effect, *definition, "density"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString timeExpr = hasTime ? "t" : "0.0";
            // Material effects use the skinned local 3D position rather than mesh UVs.
            // That keeps procedural lines continuous across duplicated UV seams and
            // avoids the latitude/pole collapse that motivated the seamless GLSL path.
            const QString phase = project.target == Target::Material
                ? QString("surfacePosition.z * 0.02 * %1").arg(density)
                : QString("uv.y * %1").arg(density);
            out += QString("    // %1\n    float %2_scan = 0.5 + 0.5 * sin((%3 + %4 * %5) * 6.2831853);\n"
                           "    color *= 1.0 - %6 * (0.35 + 0.65 * %2_scan);\n")
                .arg(definition->name, tag, phase, timeExpr, speed, amount);
        }
        else if(effect.typeId == "pulse" && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1\n    float %2_pulse = 0.5 + 0.5 * sin(t * %3 * 6.2831853);\n"
                           "    color *= lerp(1.0 - %4, 1.0 + %4, %2_pulse);\n")
                .arg(definition->name, tag, speed, amount);
        }
        else if(effect.typeId == "noise" && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString timeExpr = hasTime ? QString("floor(t * %1 * 24.0)").arg(speed) : "0.0";
            if(project.target == Target::Material)
            {
                out += QString("    // %1 (seamless local 3D noise)\n"
                               "    float %2_noise = BO3BeginnerHash31(floor(surfacePosition * (0.01 * %3)) + %4);\n"
                               "    color += (%2_noise - 0.5) * %5;\n")
                    .arg(definition->name, tag, scale, timeExpr, amount);
            }
            else
            {
                out += QString("    // %1\n    float %2_noise = BO3BeginnerHash21(floor(uv * %3) + %4);\n"
                               "    color += (%2_noise - 0.5) * %5;\n")
                    .arg(definition->name, tag, scale, timeExpr, amount);
            }
        }
        else if(effect.typeId == "film_grain" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString grainSize = floatLiteral(parameterFloat(effect, *definition, "grain_size"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1 - layered pixel-space grain, modulated like photographed film\n"
                           "    float %2_fine = BO3BeginnerGrainLayer(uv, %3 * 1.00, 0.35, 0.0, %4);\n"
                           "    float %2_medium = BO3BeginnerGrainLayer(uv, %3 * 1.56, 1.91, 7.0, %4);\n"
                           "    float %2_coarse = BO3BeginnerGrainLayer(uv, %3 * 0.67, -1.17, 13.0, %4);\n"
                           "    float %2_grain = %2_fine * 0.58 + %2_medium * 0.27 + %2_coarse * 0.15;\n"
                           "    float3 %2_responseColor = max(color, 0.0) / (1.0 + max(color, 0.0));\n"
                           "    float %2_luma = dot(%2_responseColor, float3(0.299, 0.587, 0.114));\n"
                           "    float %2_midtone = pow(saturate(1.0 - abs(%2_luma * 2.0 - 1.0)), 1.35);\n"
                           "    float %2_shadow = 1.0 - smoothstep(0.35, 0.95, %2_luma);\n"
                           "    float %2_response = saturate(0.18 + %2_midtone * 0.67 + %2_shadow * 0.15);\n"
                           "    color += color * %2_grain * (%5 * 0.055) * %2_response;\n"
                           "    color += %2_grain.xxx * (%5 * 0.0032) * %2_shadow;\n")
                .arg(definition->name, tag, grainSize, speed, amount);
        }
        else if(effect.typeId == "uv_scroll" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString speedX = floatLiteral(parameterFloat(effect, *definition, "speed_x"));
            const QString speedY = floatLiteral(parameterFloat(effect, *definition, "speed_y"));
            out += QString("    // %1\n"
                           "    float2 %2_scrollUv = frac(uv + float2(%3, %4) * t);\n"
                           "    float3 %2_scrollBase = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv)).rgb);\n"
                           "    float3 %2_scrollColor = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, %2_scrollUv).rgb);\n"
                           "    color += (%2_scrollColor - %2_scrollBase) * %5;\n")
                .arg(definition->name, tag, speedX, speedY, amount);
        }
        else if(effect.typeId == "wave_ripple" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString frequency = floatLiteral(parameterFloat(effect, *definition, "frequency"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1\n"
                           "    float2 %2_rippleP = uv - 0.5;\n"
                           "    float %2_rippleD = max(length(%2_rippleP), 0.0001);\n"
                           "    float %2_rippleWave = sin((%2_rippleD * %3 - t * %4) * 6.2831853);\n"
                           "    float2 %2_rippleUv = saturate(uv + (%2_rippleP / %2_rippleD) * %2_rippleWave * %5);\n"
                           "    float3 %2_rippleBase = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv)).rgb);\n"
                           "    float3 %2_rippleColor = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, %2_rippleUv).rgb);\n"
                           "    color += (%2_rippleColor - %2_rippleBase);\n")
                .arg(definition->name, tag, frequency, speed, amount);
        }
        else if(effect.typeId == "flicker" && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1\n"
                           "    float %2_flicker = BO3BeginnerHash11(floor(t * %3 * 30.0));\n"
                           "    color *= lerp(1.0 - %4, 1.0 + %4, %2_flicker);\n")
                .arg(definition->name, tag, speed, amount);
        }
        else if(effect.typeId == "chromatic_aberration" && project.target == Target::PostFx && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n"
                           "    float2 %2_chromaOffset = float2(%3, 0.0);\n"
                           "    float3 %2_chromaBase = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv)).rgb);\n"
                           "    float3 %2_chromaR = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + %2_chromaOffset)).rgb);\n"
                           "    float3 %2_chromaB = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv - %2_chromaOffset)).rgb);\n"
                           "    float3 %2_chroma = float3(%2_chromaR.r, %2_chromaBase.g, %2_chromaB.b);\n"
                           "    color += (%2_chroma - %2_chromaBase) * %4;\n")
                .arg(definition->name, tag, amount, strength);
        }
        else if(effect.typeId == "edge_glow" && project.target == Target::Material)
        {
            const QColor glowColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString power = floatLiteral(parameterFloat(effect, *definition, "power"));
            out += QString("    // %1\n"
                           "    float %2_rim = pow(1.0 - saturate(dot(surfaceNormal, surfaceViewDir)), %3);\n"
                           "    color += %4 * (%2_rim * %5);\n")
                .arg(definition->name, tag, power, colorLiteral(glowColor), strength);
        }
        else if(effect.typeId == "emission" && project.target == Target::Material)
        {
            const QColor emissionColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n    color += %2 * %3;\n")
                .arg(definition->name, colorLiteral(emissionColor), strength);
        }
        else if(effect.typeId == "dissolve" && project.target == Target::Material)
        {
            const QColor edgeColor = parameterColor(effect, *definition, "edge_color");
            const QString threshold = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString edgeWidth = floatLiteral(parameterFloat(effect, *definition, "edge_width"));
            out += QString("    // %1\n"
                           "    float %2_dissolveNoise = BO3BeginnerHash31(floor(surfacePosition * (0.01 * %3)));\n"
                           "    float %2_dissolveEdge = 1.0 - smoothstep(%4, min(%4 + %5, 1.0), %2_dissolveNoise);\n"
                           "    clip(%2_dissolveNoise - %4);\n"
                           "    color += %6 * (%2_dissolveEdge * 1.6);\n")
                .arg(definition->name, tag, scale, threshold, edgeWidth, colorLiteral(edgeColor));
        }
        else if(effect.typeId == "gradient")
        {
            const QColor bottom = parameterColor(effect, *definition, "bottom_color");
            const QColor top = parameterColor(effect, *definition, "top_color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n"
                           "    float3 %2_gradient = lerp(%3, %4, saturate(beginnerVertical));\n"
                           "    color = lerp(color, color * (%2_gradient * 2.0), %5);\n")
                .arg(definition->name, tag, colorLiteral(bottom), colorLiteral(top), strength);
        }
        else if(effect.typeId == "cartoon_outlines" && project.target == Target::PostFx && hasUv)
        {
            const QColor outlineColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString thickness = floatLiteral(parameterFloat(effect, *definition, "thickness"));
            const QString threshold = floatLiteral(parameterFloat(effect, *definition, "depth_threshold"));
            const QString levels = floatLiteral(parameterFloat(effect, *definition, "levels"));
            const QString detail = floatLiteral(parameterFloat(effect, *definition, "detail_edges"));
            const QString celAmount = floatLiteral(parameterFloat(effect, *definition, "cel_amount"));
            out += QString("    // %1 - point-sampled Float-Z silhouettes plus optional image-detail ink\n"
                           "    float2 %2_texel = PostFx_GetRenderTargetSize().zw * max(%3, 0.5);\n"
                           "    float2 %2_diag = %2_texel * 0.75;\n"
                           "    float %2_rawBL = BO3BeginnerSampleRawDepthPoint(uv + float2(-%2_diag.x, %2_diag.y));\n"
                           "    float %2_rawTR = BO3BeginnerSampleRawDepthPoint(uv + float2( %2_diag.x,-%2_diag.y));\n"
                           "    float %2_rawBR = BO3BeginnerSampleRawDepthPoint(uv + float2( %2_diag.x, %2_diag.y));\n"
                           "    float %2_rawTL = BO3BeginnerSampleRawDepthPoint(uv + float2(-%2_diag.x,-%2_diag.y));\n"
                           "    float %2_d0 = FloatZ_Process(%2_rawBL);\n"
                           "    float %2_d1 = FloatZ_Process(%2_rawTR);\n"
                           "    float %2_d2 = FloatZ_Process(%2_rawBR);\n"
                           "    float %2_d3 = FloatZ_Process(%2_rawTL);\n"
                           "    float %2_fd0 = %2_d1 - %2_d0;\n"
                           "    float %2_fd1 = %2_d3 - %2_d2;\n"
                           "    float %2_depthMagnitude = length(float2(%2_fd0,%2_fd1)) * 100.0;\n"
                           "    float %2_depthReference = max(min(min(%2_d0,%2_d1),min(%2_d2,%2_d3)), 0.0001);\n"
                           "    float %2_depthThreshold = max(0.015, %4 * %2_depthReference);\n"
                           "    float %2_depthEdge = smoothstep(%2_depthThreshold * 0.78, %2_depthThreshold * 1.32, %2_depthMagnitude);\n"
                           "    float3 %2_sceneL = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv - float2(%2_texel.x,0.0))).rgb);\n"
                           "    float3 %2_sceneR = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(%2_texel.x,0.0))).rgb);\n"
                           "    float3 %2_sceneU = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv - float2(0.0,%2_texel.y))).rgb);\n"
                           "    float3 %2_sceneD = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(0.0,%2_texel.y))).rgb);\n"
                           "    float3 %2_lumaWeights = float3(0.299,0.587,0.114);\n"
                           "    float %2_gx = dot(%2_sceneR-%2_sceneL, %2_lumaWeights);\n"
                           "    float %2_gy = dot(%2_sceneD-%2_sceneU, %2_lumaWeights);\n"
                           "    float %2_detailEdge = smoothstep(0.045, 0.145, length(float2(%2_gx,%2_gy))) * %5;\n"
                           "    float %2_edge = saturate(max(%2_depthEdge, %2_detailEdge));\n"
                           "    float %2_steps = max(2.0, round(%6));\n"
                           "    float %2_luma = max(dot(color, %2_lumaWeights), 0.0001);\n"
                           "    float %2_qLuma = floor(saturate(%2_luma) * (%2_steps - 1.0) + 0.5) / (%2_steps - 1.0);\n"
                           "    float3 %2_toon = color * (%2_qLuma / %2_luma);\n"
                           "    color = lerp(color, %2_toon, %7);\n"
                           "    color = lerp(color, %8, saturate(%2_edge * %9));\n")
                .arg(definition->name, tag, thickness, threshold, detail, levels, celAmount, colorLiteral(outlineColor), strength);
        }
        else if(effect.typeId == "ambient_occlusion" && project.target == Target::PostFx && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString radius = floatLiteral(parameterFloat(effect, *definition, "radius"));
            const QString bias = floatLiteral(parameterFloat(effect, *definition, "bias"));
            out += QString("    // %1 - 12-tap opposing-pair Float-Z SSAO/contact shading\n"
                           "    float %2_rawCenter = BO3BeginnerSampleRawDepthPoint(uv);\n"
                           "    float %2_worldMask = %2_rawCenter < BO3_BEGINNER_FLOATZ_DEPTHHACK_SPLIT ? 1.0 : 0.0;\n"
                           "    float %2_centerDepth = BO3BeginnerLinearDepth(%2_rawCenter);\n"
                           "    float %2_surfaceMask = %2_worldMask * (1.0 - smoothstep(6200.0, 12500.0, %2_centerDepth));\n"
                           "    float %2_depthRadiusScale = clamp(sqrt(430.0 / max(%2_centerDepth, 1.0)), 0.52, 1.85);\n"
                           "    float %2_radiusPixels = max(%3, 0.5) * %2_depthRadiusScale;\n"
                           "    float %2_depthBias = max(0.22 + %4 * 1.80, %2_centerDepth * (0.00040 + %4 * 0.0012));\n"
                           "    float %2_depthRange = max(lerp(7.0, 22.0, %5), %2_centerDepth * lerp(0.016, 0.046, %5));\n"
                           "    float2 %2_pixel = floor(uv * PostFx_GetRenderTargetSize().xy);\n"
                           "    float %2_phase = BO3BeginnerSSAOHash12(%2_pixel * 0.0713) * 6.28318530718;\n"
                           "    float %2_pairSum = 0.0;\n"
                           "    [unroll] for(int %2_i=0; %2_i<6; ++%2_i)\n"
                           "    {\n"
                           "        float %2_progress = (float(%2_i) + 1.0) / 6.0;\n"
                           "        float %2_pairRadius = sqrt(%2_progress) * %2_radiusPixels;\n"
                           "        float2 %2_dir = float2(sin(%2_phase), cos(%2_phase));\n"
                           "        float2 %2_offset = %2_dir * PostFx_GetRenderTargetSize().zw * %2_pairRadius;\n"
                           "        float %2_depthA = BO3BeginnerSampleWorldDepth(uv + %2_offset);\n"
                           "        float %2_depthB = BO3BeginnerSampleWorldDepth(uv - %2_offset);\n"
                           "        float %2_pairWeight = lerp(1.18, 0.72, %2_progress);\n"
                           "        %2_pairSum += BO3BeginnerSSAOPair(%2_centerDepth, %2_depthA, %2_depthB, %2_depthBias, %2_depthRange) * %2_pairWeight;\n"
                           "        %2_phase += 2.39996322973;\n"
                           "    }\n"
                           "    float %2_occ = saturate((%2_pairSum / 5.70) * lerp(2.70, 6.80, %5));\n"
                           "    %2_occ = pow(%2_occ, lerp(1.30, 0.72, %5));\n"
                           "    float2 %2_edgeUv = min(uv, 1.0 - uv);\n"
                           "    float %2_edgePixels = min(%2_edgeUv.x / max(PostFx_GetRenderTargetSize().z,1e-6), %2_edgeUv.y / max(PostFx_GetRenderTargetSize().w,1e-6));\n"
                           "    float %2_frameMask = smoothstep(2.0, 14.0, %2_edgePixels);\n"
                           "    float %2_darkening = %2_occ * %2_surfaceMask * %2_frameMask * lerp(0.18, 0.78, %5);\n"
                           "    color *= 1.0 - saturate(%2_darkening);\n")
                .arg(definition->name, tag, radius, bias, amount);
        }
        else if(effect.typeId == "depth_fog" && project.target == Target::PostFx && hasUv)
        {
            const QColor fogColor = parameterColor(effect, *definition, "color");
            const QString startControl = floatLiteral(parameterFloat(effect, *definition, "start"));
            const QString endControl = floatLiteral(parameterFloat(effect, *definition, "end"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString falloff = floatLiteral(parameterFloat(effect, *definition, "falloff"));
            out += QString("    // %1 - true linear Float-Z distance fog\n"
                           "    float %2_rawDepth = BO3BeginnerSampleRawDepthPoint(uv);\n"
                           "    float %2_worldDepth = %2_rawDepth < BO3_BEGINNER_FLOATZ_DEPTHHACK_SPLIT ? BO3BeginnerLinearDepth(%2_rawDepth) : -1.0;\n"
                           "    float %2_startWorld = BO3BeginnerDepthControlToWorld(%3);\n"
                           "    float %2_endWorld = max(%2_startWorld + 1.0, BO3BeginnerDepthControlToWorld(max(%4, %3 + 0.01)));\n"
                           "    float %2_valid = step(0.0001, %2_worldDepth);\n"
                           "    float %2_fog = smoothstep(%2_startWorld, %2_endWorld, max(%2_worldDepth,0.0));\n"
                           "    %2_fog = pow(saturate(%2_fog), max(%5,0.05)) * %2_valid * %6;\n"
                           "    color = lerp(color, %7, saturate(%2_fog));\n")
                .arg(definition->name, tag, startControl, endControl, falloff, strength, colorLiteral(fogColor));
        }
        else if(effect.typeId == "luminance_tint")
        {
            const QColor shadowColor = parameterColor(effect, *definition, "shadow_color");
            const QColor highlightColor = parameterColor(effect, *definition, "highlight_color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString contrast = floatLiteral(parameterFloat(effect, *definition, "contrast"));
            out += QString("    // %1\n"
                           "    float %2_luma = saturate(dot(saturate(color), float3(0.2126, 0.7152, 0.0722)));\n"
                           "    float %2_t = saturate((%2_luma - 0.5) * %3 + 0.5);\n"
                           "    float3 %2_tint = lerp(%4, %5, %2_t);\n"
                           "    color = lerp(color, color * (%2_tint * 1.6), %6);\n")
                .arg(definition->name, tag, contrast, colorLiteral(shadowColor), colorLiteral(highlightColor), strength);
        }
        else if(effect.typeId == "luminance_sharpness" && project.target == Target::PostFx && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString radius = floatLiteral(parameterFloat(effect, *definition, "radius"));
            const QString threshold = floatLiteral(parameterFloat(effect, *definition, "threshold"));
            out += QString("    // %1 - luminance-only detail enhancement that scales RGB together\n"
                           "    float2 %2_step = PostFx_GetRenderTargetSize().zw * max(%3,0.5);\n"
                           "    float3 %2_n = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(0.0,-%2_step.y))).rgb);\n"
                           "    float3 %2_s = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(0.0, %2_step.y))).rgb);\n"
                           "    float3 %2_w = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(-%2_step.x,0.0))).rgb);\n"
                           "    float3 %2_e = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2( %2_step.x,0.0))).rgb);\n"
                           "    float3 %2_lw = float3(0.2126,0.7152,0.0722);\n"
                           "    float %2_cL = max(dot(color,%2_lw),0.0001);\n"
                           "    float %2_nL = dot(%2_n,%2_lw); float %2_sL = dot(%2_s,%2_lw);\n"
                           "    float %2_wL = dot(%2_w,%2_lw); float %2_eL = dot(%2_e,%2_lw);\n"
                           "    float %2_avg = (%2_nL+%2_sL+%2_wL+%2_eL)*0.25;\n"
                           "    float %2_minL = min(%2_cL,min(min(%2_nL,%2_sL),min(%2_wL,%2_eL)));\n"
                           "    float %2_maxL = max(%2_cL,max(max(%2_nL,%2_sL),max(%2_wL,%2_eL)));\n"
                           "    float %2_range = max(%2_maxL-%2_minL,0.0001);\n"
                           "    float %2_detail = clamp(%2_cL-%2_avg, -%2_range*0.92, %2_range*0.92);\n"
                           "    float %2_gate = smoothstep(%4, max(%4*6.0, %4+0.001), %2_range);\n"
                           "    float %2_sharpL = max(%2_cL + %2_detail * %5 * (1.05 + %5*0.16) * %2_gate, 0.0);\n"
                           "    float %2_ratio = clamp(%2_sharpL / max(%2_cL,0.025), 0.45, 2.75);\n"
                           "    color = max(color * %2_ratio, 0.0);\n")
                .arg(definition->name, tag, radius, threshold, amount);
        }
        else if(effect.typeId == "posterize")
        {
            const QString levels = floatLiteral(parameterFloat(effect, *definition, "levels"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n"
                           "    float %2_levels = max(2.0, round(%3));\n"
                           "    float3 %2_poster = floor(saturate(color) * (%2_levels - 1.0) + 0.5) / (%2_levels - 1.0);\n"
                           "    color = lerp(color, %2_poster, %4);\n")
                .arg(definition->name, tag, levels, strength);
        }
        else if(effect.typeId == "fisheye" && project.target == Target::PostFx && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString zoom = floatLiteral(parameterFloat(effect, *definition, "zoom"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1 - signed normalized radial lens curve\n"
                           "    float2 %2_center = float2(0.5,0.5);\n"
                           "    float2 %2_delta = uv - %2_center;\n"
                           "    float %2_r = length(%2_delta);\n"
                           "    float %2_signed = clamp(%3,-10.0,10.0);\n"
                           "    float %2_mag = abs(%2_signed);\n"
                           "    float %2_eff = %2_mag / (1.0 + max(%2_mag-1.0,0.0)*0.18);\n"
                           "    float %2_linear = 0.55 * %2_eff;\n"
                           "    float %2_cubic = 0.25 * %2_eff;\n"
                           "    float %2_curve = 1.0 + %2_r*%2_linear + (%2_r*%2_r*%2_r)*%2_cubic;\n"
                           "    const float %2_cornerR = 0.70710678;\n"
                           "    const float %2_sideR = 0.5;\n"
                           "    float %2_cornerCurve = 1.0 + %2_cornerR*%2_linear + (%2_cornerR*%2_cornerR*%2_cornerR)*%2_cubic;\n"
                           "    float %2_sideCurve = 1.0 + %2_sideR*%2_linear + (%2_sideR*%2_sideR*%2_sideR)*%2_cubic;\n"
                           "    float2 %2_positiveUv = %2_center + %2_delta * (%2_curve / max(%2_cornerCurve,0.0001));\n"
                           "    float2 %2_negativeUv = %2_center + %2_delta * (%2_sideCurve / max(%2_curve,0.0001));\n"
                           "    float2 %2_lensUv = %2_signed >= 0.0 ? %2_positiveUv : %2_negativeUv;\n"
                           "    %2_lensUv = %2_center + (%2_lensUv-%2_center) / max(%4,0.05);\n"
                           "    float2 %2_halfTexel = PostFx_GetRenderTargetSize().zw * 0.5;\n"
                           "    %2_lensUv = clamp(%2_lensUv,%2_halfTexel,1.0-%2_halfTexel);\n"
                           "    float3 %2_sample = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler,%2_lensUv).rgb);\n"
                           "    color = lerp(color,%2_sample,%5);\n")
                .arg(definition->name, tag, amount, zoom, strength);
        }
        else if(effect.typeId == "paint_strokes" && project.target == Target::PostFx && hasUv)
        {
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString smear = floatLiteral(parameterFloat(effect, *definition, "smear"));
            const QString bend = floatLiteral(parameterFloat(effect, *definition, "bend"));
            const QString detail = floatLiteral(parameterFloat(effect, *definition, "detail"));
            out += QString("    // %1 - gradient-aligned brush strokes with jittered multi-scale cells\n"
                           "    float2 %2_rt = PostFx_GetRenderTargetSize().xy;\n"
                           "    float2 %2_texel = PostFx_GetRenderTargetSize().zw;\n"
                           "    float2 %2_cells = float2(%3, max(1.0, %3 * %2_rt.y / max(%2_rt.x,1.0)));\n"
                           "    float2 %2_cellId = floor(uv * %2_cells);\n"
                           "    float2 %2_center = (%2_cellId + 0.5) / %2_cells;\n"
                           "    float2 %2_jitter = float2(BO3BeginnerHash21(%2_cellId + 2.7), BO3BeginnerHash21(%2_cellId + 8.3)) - 0.5;\n"
                           "    %2_center += %2_jitter / %2_cells * 0.72;\n"
                           "    float2 %2_gradStep = %2_texel * lerp(1.0, 4.0, %7);\n"
                           "    float3 %2_left = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center - float2(%2_gradStep.x,0))).rgb);\n"
                           "    float3 %2_right = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center + float2(%2_gradStep.x,0))).rgb);\n"
                           "    float3 %2_up = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center - float2(0,%2_gradStep.y))).rgb);\n"
                           "    float3 %2_down = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center + float2(0,%2_gradStep.y))).rgb);\n"
                           "    float2 %2_grad = float2(dot(%2_right-%2_left,float3(0.299,0.587,0.114)), dot(%2_down-%2_up,float3(0.299,0.587,0.114)));\n"
                           "    float2 %2_normal = normalize(%2_grad + float2(1e-5,0.0));\n"
                           "    float2 %2_tangent = float2(-%2_normal.y, %2_normal.x);\n"
                           "    float2 %2_local = (uv - %2_center) * %2_cells;\n"
                           "    float2 %2_brush = float2(dot(%2_local,%2_normal), dot(%2_local,%2_tangent));\n"
                           "    %2_brush.x += %2_brush.y * %2_brush.y * %6 * 0.22;\n"
                           "    float %2_width = 0.30 + BO3BeginnerHash21(%2_cellId + 4.1) * 0.18;\n"
                           "    float %2_length = 0.75 + BO3BeginnerHash21(%2_cellId + 6.4) * 0.55;\n"
                           "    float %2_mask = 1.0 - smoothstep(0.78, 1.02, length(float2(%2_brush.x/max(%2_width,0.01), %2_brush.y/max(%2_length,0.01))));\n"
                           "    float2 %2_smearUv = %2_tangent * %2_texel * (%5 * 12.0);\n"
                           "    float3 %2_s0 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center - %2_smearUv)).rgb);\n"
                           "    float3 %2_s1 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center)).rgb);\n"
                           "    float3 %2_s2 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center + %2_smearUv)).rgb);\n"
                           "    float3 %2_stroke = (%2_s0 + %2_s1 * 2.0 + %2_s2) * 0.25;\n"
                           "    float %2_texture = 0.88 + 0.12 * sin((%2_brush.y * 17.0 + %2_brush.x * 4.0) + BO3BeginnerHash21(%2_cellId) * 6.2831853);\n"
                           "    %2_stroke *= %2_texture;\n"
                           "    float3 %2_base = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_center)).rgb);\n"
                           "    float3 %2_paint = lerp(%2_base, %2_stroke, %2_mask);\n"
                           "    color = lerp(color, %2_paint, %4);\n")
                .arg(definition->name, tag, scale, strength, smear, bend, detail);
        }
        else if(effect.typeId == "red_paint_splatter" && project.target == Target::PostFx && hasUv)
        {
            const QColor splatColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            out += QString("    // %1\n"
                           "    float2 %2_p = uv * %3;\n"
                           "    float2 %2_cell = floor(%2_p);\n"
                           "    float2 %2_local = frac(%2_p) - 0.5;\n"
                           "    float %2_splat = 0.0;\n"
                           "    [unroll] for(int %2_y = -1; %2_y <= 1; ++%2_y)\n"
                           "    {\n"
                           "        [unroll] for(int %2_x = -1; %2_x <= 1; ++%2_x)\n"
                           "        {\n"
                           "            float2 %2_id = %2_cell + float2(%2_x, %2_y);\n"
                           "            float2 %2_center = (float2(BO3BeginnerHash21(%2_id + 1.7), BO3BeginnerHash21(%2_id + 9.2)) - 0.5) * 0.9;\n"
                           "            float %2_radius = 0.14 + BO3BeginnerHash21(%2_id + 4.6) * 0.28;\n"
                           "            float2 %2_delta = %2_local - float2(%2_x, %2_y) - %2_center;\n"
                           "            float %2_blob = 1.0 - smoothstep(%2_radius, %2_radius + 0.12, length(%2_delta));\n"
                           "            float %2_drip = 1.0 - smoothstep(0.02, 0.11, abs(%2_delta.x)) * smoothstep(-0.38, 0.24, %2_delta.y);\n"
                           "            %2_splat = max(%2_splat, max(%2_blob, %2_drip * 0.55 * step(0.0, %2_delta.y)));\n"
                           "        }\n"
                           "    }\n"
                           "    color = lerp(color, color * 0.35 + %4 * 0.9, saturate(%2_splat * %5));\n")
                .arg(definition->name, tag, scale, colorLiteral(splatColor), strength);
        }
        else if(effect.typeId == "water_distortion" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n"
                           "    float2 %2_wave = float2(\n"
                           "        sin((uv.y * %3 + t * %4) * 6.2831853) + cos((uv.y * (%3 * 0.47) - t * %4 * 0.6) * 6.2831853),\n"
                           "        cos((uv.x * %3 - t * %4 * 0.8) * 6.2831853) + sin((uv.x * (%3 * 0.63) + t * %4 * 0.45) * 6.2831853));\n"
                           "    float2 %2_uv = saturate(uv + %2_wave * (%5 * 0.01));\n"
                           "    float3 %2_sample = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, %2_uv).rgb);\n"
                           "    color = lerp(color, %2_sample, %6);\n")
                .arg(definition->name, tag, scale, speed, amount, strength);
        }
        else if(effect.typeId == "psx_dithering" && project.target == Target::PostFx && hasUv)
        {
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString precision = floatLiteral(parameterFloat(effect, *definition, "color_precision"));
            out += QString("    // %1 - ordered 4x4 screen-space dithering\n"
                           "    int %2_dx = ((int)input.position.x) & 3;\n"
                           "    int %2_dy = ((int)input.position.y) & 3;\n"
                           "    float %2_d = BO3BeginnerPsxDither[%2_dx][%2_dy];\n"
                           "    float3 %2_rgb255 = saturate(color) * 255.0;\n"
                           "    float3 %2_dithered = saturate((%2_rgb255 + (%2_d * 0.5 - 4.0)) / 255.0);\n"
                           "    float3 %2_lowPrecision = floor(%2_dithered * 31.0 + 0.5) / 31.0;\n"
                           "    %2_dithered = lerp(%2_dithered, %2_lowPrecision, %3);\n"
                           "    color = lerp(color, %2_dithered, %4);\n")
                .arg(definition->name, tag, precision, strength);
        }
        else if(effect.typeId == "sharpness" && project.target == Target::PostFx && hasUv)
        {
            const QString amount = floatLiteral(parameterFloat(effect, *definition, "amount"));
            const QString threshold = floatLiteral(parameterFloat(effect, *definition, "threshold"));
            const QString radius = floatLiteral(parameterFloat(effect, *definition, "radius"));
            out += QString("    // %1 - edge-aware 8-neighbor sharpen\n"
                           "    float2 %2_step = PostFx_GetRenderTargetSize().zw * %3;\n"
                           "    float3 %2_c1 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(-%2_step.x,-%2_step.y))).rgb);\n"
                           "    float3 %2_c2 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(0.0,-%2_step.y))).rgb);\n"
                           "    float3 %2_c3 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(%2_step.x,-%2_step.y))).rgb);\n"
                           "    float3 %2_c4 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(-%2_step.x,0.0))).rgb);\n"
                           "    float3 %2_c5 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(%2_step.x,0.0))).rgb);\n"
                           "    float3 %2_c6 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(-%2_step.x,%2_step.y))).rgb);\n"
                           "    float3 %2_c7 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(0.0,%2_step.y))).rgb);\n"
                           "    float3 %2_c8 = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(%2_step.x,%2_step.y))).rgb);\n"
                           "    float3 %2_d1 = %2_c6 + %2_c4 + %2_c1 - %2_c3 - %2_c5 - %2_c8;\n"
                           "    float3 %2_d2 = %2_c4 + %2_c1 + %2_c2 - %2_c5 - %2_c8 - %2_c7;\n"
                           "    float3 %2_d3 = %2_c1 + %2_c2 + %2_c3 - %2_c8 - %2_c7 - %2_c6;\n"
                           "    float3 %2_d4 = %2_c2 + %2_c3 + %2_c5 - %2_c7 - %2_c6 - %2_c4;\n"
                           "    float %2_edge = length(abs(%2_d1)+abs(%2_d2)+abs(%2_d3)+abs(%2_d4)) / 6.0;\n"
                           "    float3 %2_neighbor = (%2_c1+%2_c2+%2_c3+%2_c4+%2_c5+%2_c6+%2_c7+%2_c8) * 0.125;\n"
                           "    float3 %2_sharp = max(color + (color - %2_neighbor) * %4, 0.0);\n"
                           "    color = lerp(color, %2_sharp, smoothstep(%5, %5 * 2.0 + 0.0001, %2_edge));\n")
                .arg(definition->name, tag, radius, amount, threshold);
        }
        else if(effect.typeId == "pixel_resolution" && project.target == Target::PostFx && hasUv)
        {
            const QString width = floatLiteral(parameterFloat(effect, *definition, "width"));
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            out += QString("    // %1\n"
                           "    float2 %2_rt = PostFx_GetRenderTargetSize().xy;\n"
                           "    float2 %2_virtual = float2(%3, max(1.0, %3 * %2_rt.y / max(%2_rt.x,1.0)));\n"
                           "    float2 %2_pixelUv = (floor(uv * %2_virtual) + 0.5) / %2_virtual;\n"
                           "    float3 %2_pixelColor = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_pixelUv)).rgb);\n"
                           "    color = lerp(color, %2_pixelColor, %4);\n")
                .arg(definition->name, tag, width, strength);
        }
        else if(effect.typeId == "vhs_tape" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString jitter = floatLiteral(parameterFloat(effect, *definition, "jitter"));
            const QString chroma = floatLiteral(parameterFloat(effect, *definition, "chroma"));
            const QString tracking = floatLiteral(parameterFloat(effect, *definition, "tracking"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1 - row jitter, tracking tear and analog color separation\n"
                           "    float2 %2_rt = PostFx_GetRenderTargetSize().xy;\n"
                           "    float2 %2_texel = PostFx_GetRenderTargetSize().zw;\n"
                           "    float %2_frame = floor(t * %6 * 30.0);\n"
                           "    float %2_row = floor(uv.y * %2_rt.y * 0.25);\n"
                           "    float %2_rowNoise = BO3BeginnerHash21(float2(%2_row, %2_frame));\n"
                           "    float %2_fineNoise = BO3BeginnerHash21(float2(floor(uv.y * %2_rt.y), %2_frame * 1.73));\n"
                           "    float %2_tearWave = sin(uv.y * 8.0 - t * %6 * 3.77);\n"
                           "    float %2_tear = smoothstep(0.90, 0.995, %2_tearWave * 0.5 + 0.5) * step(0.68, %2_rowNoise) * %5;\n"
                           "    float2 %2_vhsUv = uv;\n"
                           "    %2_vhsUv.x += ((%2_rowNoise - 0.5) * 2.0 + (%2_fineNoise - 0.5)) * %3 * %2_texel.x * 14.0;\n"
                           "    %2_vhsUv.x -= %2_tear * (0.015 + %3 * 0.018);\n"
                           "    float2 %2_ca = float2(%4 * %2_texel.x * 10.0, 0.0);\n"
                           "    float3 %2_base = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_vhsUv)).rgb);\n"
                           "    float %2_r = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_vhsUv + %2_ca)).rgb).r;\n"
                           "    float %2_b = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(%2_vhsUv - %2_ca)).rgb).b;\n"
                           "    float3 %2_vhs = float3(%2_r, %2_base.g, %2_b);\n"
                           "    %2_vhs *= 0.97 + 0.06 * BO3BeginnerHash21(float2(0.0, %2_row + %2_frame));\n"
                           "    %2_vhs *= 1.0 - %2_tear * 0.62;\n"
                           "    color = lerp(color, max(%2_vhs,0.0), %7);\n")
                .arg(definition->name, tag, jitter, chroma, tracking, speed, strength);
        }
        else if(effect.typeId == "vhs_dropouts" && project.target == Target::PostFx && hasUv && hasTime)
        {
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString density = floatLiteral(parameterFloat(effect, *definition, "density"));
            const QString shift = floatLiteral(parameterFloat(effect, *definition, "shift"));
            out += QString("    // %1 - intermittent damaged-tape dropouts\n"
                           "    float2 %2_rt = PostFx_GetRenderTargetSize().xy;\n"
                           "    float %2_frame = floor(t * 24.0);\n"
                           "    float %2_row = floor(uv.y * %2_rt.y / max(2.0, %3));\n"
                           "    float %2_gate = BO3BeginnerHash21(float2(%2_row, %2_frame));\n"
                           "    float %2_active = step(0.86, %2_gate);\n"
                           "    float %2_offset = (BO3BeginnerHash21(float2(%2_row + 9.0, %2_frame)) - 0.5) * %4 * 0.08;\n"
                           "    float3 %2_shifted = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, saturate(uv + float2(%2_offset,0.0))).rgb);\n"
                           "    float %2_static = BO3BeginnerHash21(floor(input.position.xy) + %2_frame) - 0.5;\n"
                           "    float3 %2_damaged = max(%2_shifted * (0.75 + %2_static * 0.35), 0.0);\n"
                           "    color = lerp(color, %2_damaged, %2_active * %5);\n")
                .arg(definition->name, tag, density, shift, strength);
        }
        else if(effect.typeId == "sky_sun" && project.target == Target::Sky)
        {
            const QColor sunColor = parameterColor(effect, *definition, "color");
            const QString size = floatLiteral(parameterFloat(effect, *definition, "size"));
            const QString softness = floatLiteral(parameterFloat(effect, *definition, "softness"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString glow = floatLiteral(parameterFloat(effect, *definition, "glow"));
            const QString atmosphere = floatLiteral(parameterFloat(effect, *definition, "atmosphere"));
            const QString haze = floatLiteral(parameterFloat(effect, *definition, "haze"));
            out += QString("    // %1 - compact BO3 day/night atmosphere converted from the supplied Shadertoy reference\n"
                           "    float %2_mu = clamp(dot(d, beginnerSunDir), -1.0, 1.0);\n"
                           "    float %2_mu2 = %2_mu * %2_mu;\n"
                           "    float %2_horizon = pow(saturate(1.0 - abs(d.z)), 2.35);\n"
                           "    float %2_day = beginnerDaylight;\n"
                           "    float %2_twilight = exp(-abs(beginnerSunElevation) * 8.5) * (1.0 - %2_day * 0.18);\n"
                           "    float3 %2_nightZenith = float3(0.004, 0.008, 0.025);\n"
                           "    float3 %2_nightHorizon = float3(0.012, 0.014, 0.030);\n"
                           "    float3 %2_dayZenith = float3(0.085, 0.245, 0.620);\n"
                           "    float3 %2_dayHorizon = float3(0.46, 0.63, 0.82);\n"
                           "    float3 %2_zenith = lerp(%2_nightZenith, %2_dayZenith, %2_day);\n"
                           "    float3 %2_horizonColor = lerp(%2_nightHorizon, %2_dayHorizon, %2_day);\n"
                           "    float3 %2_sky = lerp(%2_horizonColor, %2_zenith, smoothstep(-0.03, 0.78, d.z));\n"
                           "    float %2_phaseR = 0.75 * (1.0 + %2_mu2);\n"
                           "    float3 %2_rayleighTint = float3(0.18, 0.42, 1.0) * %2_phaseR;\n"
                           "    %2_sky += %2_rayleighTint * (%2_day * (0.035 + %2_horizon * 0.065));\n"
                           "    float3 %2_warm = lerp(float3(1.0, 0.18, 0.035), %3, smoothstep(0.00, 0.32, saturate(beginnerSunElevation * 2.0 + 0.15)));\n"
                           "    float %2_sunward = pow(saturate(%2_mu * 0.5 + 0.5), 5.0);\n"
                           "    %2_sky += %2_warm * (%2_twilight * %2_horizon * %2_sunward * (0.22 + %4 * 0.55));\n"
                           "    const float %2_g = 0.72;\n"
                           "    float %2_mieDen = pow(max(1.0 + %2_g*%2_g - 2.0*%2_g*%2_mu, 0.001), 1.5);\n"
                           "    float %2_mie = ((1.0 - %2_g*%2_g) / %2_mieDen) * 0.045;\n"
                           "    %2_sky += %2_warm * (%2_mie * %2_day * (0.18 + %4 * 0.58));\n"
                           "    %2_sky = max(%2_sky, 0.0);\n"
                           "    color = lerp(color, %2_sky, saturate(%5));\n"
                           "    float %2_angle = acos(clamp(%2_mu, -1.0, 1.0));\n"
                           "    float %2_discRadius = max(%6, 0.0005);\n"
                           "    float %2_disc = 1.0 - smoothstep(%2_discRadius, %2_discRadius + max(%7, 0.0004), %2_angle);\n"
                           "    float %2_discNorm = saturate(%2_angle / %2_discRadius);\n"
                           "    float %2_limb = sqrt(saturate(1.0 - %2_discNorm * %2_discNorm));\n"
                           "    float %2_softHalo = exp(-%2_angle * lerp(72.0, 20.0, saturate(%4)));\n"
                           "    float %2_sunVisible = smoothstep(-0.018, 0.010, beginnerSunElevation);\n"
                           "    float %2_discEnergy = 0.16 + %8 * 0.14;\n"
                           "    float %2_haloEnergy = (0.012 + %8 * 0.007) * %4;\n"
                           "    float %2_discShape = %2_disc * (0.72 + 0.28 * %2_limb);\n"
                           "    color += %2_warm * (%2_discShape * %2_discEnergy + %2_softHalo * %2_haloEnergy) * %2_sunVisible;\n"
                           "    color = lerp(color, color + %2_warm * 0.12, %2_horizon * saturate(%9) * %2_twilight * 0.52);\n")
                .arg(definition->name, tag, colorLiteral(sunColor), glow, atmosphere, size, softness, brightness, haze);
        }
        else if(effect.typeId == "sky_moon" && project.target == Target::Sky)
        {
            const QColor moonColor = parameterColor(effect, *definition, "color");
            const QString azimuth = floatLiteral(parameterFloat(effect, *definition, "azimuth"));
            const QString height = floatLiteral(parameterFloat(effect, *definition, "height"));
            const QString size = floatLiteral(parameterFloat(effect, *definition, "size"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString halo = floatLiteral(parameterFloat(effect, *definition, "halo"));
            const QString phase = floatLiteral(parameterFloat(effect, *definition, "phase"));
            out += QString("    // %1 - independently positioned moon disc with crescent phase and halo\n"
                           "    float %2_az = %3 * 6.2831853;\n"
                           "    float %2_h = clamp(%4, -0.98, 0.98);\n"
                           "    float %2_xyLen = sqrt(max(1.0 - %2_h * %2_h, 0.0));\n"
                           "    float3 %2_dir = normalize(float3(cos(%2_az) * %2_xyLen, sin(%2_az) * %2_xyLen, %2_h));\n"
                           "    float %2_angle = acos(clamp(dot(d, %2_dir), -1.0, 1.0));\n"
                           "    float %2_disc = 1.0 - smoothstep(%5, %5 + max(%5*0.18,0.002), %2_angle);\n"
                           "    float3 %2_right = normalize(float3(-sin(%2_az), cos(%2_az), 0.0));\n"
                           "    float %2_phaseCoord = dot(d, %2_right) / max(sin(max(%5,0.002)), 0.002);\n"
                           "    float %2_lit = smoothstep(-0.15, 0.15, %2_phaseCoord + lerp(1.5, -1.5, %6));\n"
                           "    float %2_moon = %2_disc * lerp(1.0, %2_lit, abs(%6 - 0.5) * 1.8);\n"
                           "    float %2_halo = pow(saturate(dot(d,%2_dir)), lerp(82.0, 7.0, %7));\n"
                           "    color += %8 * (%2_moon * %9 + %2_halo * %9 * %7 * 0.20);\n")
                .arg(definition->name, tag, azimuth, height, size, phase, halo, colorLiteral(moonColor), brightness);
        }
        else if(effect.typeId == "sky_haze" && project.target == Target::Sky)
        {
            const QColor hazeColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString width = floatLiteral(parameterFloat(effect, *definition, "width"));
            out += QString("    // %1\n"
                           "    float %2_haze = pow(saturate(1.0 - abs(d.z)), %3);\n"
                           "    color = lerp(color, %4, saturate(%2_haze * %5));\n")
                .arg(definition->name, tag, width, colorLiteral(hazeColor), strength);
        }
        else if(effect.typeId == "sky_stars" && project.target == Target::Sky)
        {
            const QColor starColor = parameterColor(effect, *definition, "color");
            const QString density = floatLiteral(parameterFloat(effect, *definition, "density"));
            const QString size = floatLiteral(parameterFloat(effect, *definition, "size"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString twinkle = floatLiteral(parameterFloat(effect, *definition, "twinkle"));
            out += QString("    // %1 - direction-space star field\n"
                           "    float3 %2_sp = d * %3;\n"
                           "    float3 %2_cell = floor(%2_sp);\n"
                           "    float3 %2_local = frac(%2_sp) - 0.5;\n"
                           "    float %2_rnd = BO3BeginnerHash31(%2_cell);\n"
                           "    float %2_star = step(0.985, %2_rnd) * (1.0 - smoothstep(%4, %4 * 2.2, length(%2_local)));\n"
                           "    float %2_tw = lerp(1.0, 0.65 + 0.35 * sin(t * 2.7 + %2_rnd * 41.0), %5);\n"
                           "    color += %6 * (%2_star * %7 * %2_tw);\n")
                .arg(definition->name, tag, density, size, twinkle, colorLiteral(starColor), brightness);
        }
        else if(effect.typeId == "sky_clouds" && project.target == Target::Sky)
        {
            const QColor cloudColor = parameterColor(effect, *definition, "color");
            const QString opacity = floatLiteral(parameterFloat(effect, *definition, "opacity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString coverage = floatLiteral(parameterFloat(effect, *definition, "coverage"));
            const QString softness = floatLiteral(parameterFloat(effect, *definition, "softness"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString height = floatLiteral(parameterFloat(effect, *definition, "height"));
            const QString direction = floatLiteral(parameterFloat(effect, *definition, "direction"));
            out += QString("    // %1 - perspective cloud sheet with directional wind and sun response\n"
                           "    float %2_windAngle = %9 * 0.01745329252;\n"
                           "    float2 %2_wind = float2(cos(%2_windAngle), sin(%2_windAngle));\n"
                           "    float %2_layerHeight = 1.35 + %8 * 2.2;\n"
                           "    float %2_cloudT = %2_layerHeight / max(d.z, 0.035);\n"
                           "    float2 %2_world = d.xy * %2_cloudT;\n"
                           "    float3 %2_cp = float3((%2_world + %2_wind * (t * %6 * 0.32)) * (%3 * 0.22), 0.65);\n"
                           "    float %2_n1 = BO3BeginnerFbm3(%2_cp);\n"
                           "    float %2_n2 = BO3BeginnerFbm3(%2_cp * 2.03 + float3(4.1,1.7,-2.4));\n"
                           "    float %2_density = %2_n1 * 0.73 + %2_n2 * 0.27;\n"
                           "    float %2_cloud = smoothstep(%4, min(%4 + max(%5,0.01), 1.0), %2_density);\n"
                           "    float %2_skyMask = smoothstep(0.018, 0.095, d.z);\n"
                           "    float %2_sunFacing = saturate(dot(normalize(float3(%2_wind * 0.15, 0.98)), beginnerSunDir) * 0.5 + 0.5);\n"
                           "    float %2_internalLight = 0.62 + 0.38 * BO3BeginnerFbm3(%2_cp + beginnerSunDir * 0.42);\n"
                           "    float3 %2_cloudColor = %7 * %10 * lerp(0.68, 1.16, %2_internalLight * lerp(0.55,1.0,beginnerDaylight));\n"
                           "    %2_cloudColor *= lerp(0.72, 1.12, %2_sunFacing * beginnerDaylight);\n"
                           "    color = lerp(color, %2_cloudColor, saturate(%2_cloud * %2_skyMask * %11));\n")
                .arg(definition->name)
                .arg(tag)
                .arg(scale)
                .arg(coverage)
                .arg(softness)
                .arg(speed)
                .arg(colorLiteral(cloudColor))
                .arg(height)
                .arg(direction)
                .arg(brightness)
                .arg(opacity);
        }
        else if(effect.typeId == "sky_realistic_clouds" && project.target == Target::Sky)
        {
            const QColor shadowColor = parameterColor(effect, *definition, "shadow_color");
            const QColor lightColor = parameterColor(effect, *definition, "light_color");
            const QString opacity = floatLiteral(parameterFloat(effect, *definition, "opacity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString coverage = floatLiteral(parameterFloat(effect, *definition, "coverage"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString height = floatLiteral(parameterFloat(effect, *definition, "height"));
            const QString direction = floatLiteral(parameterFloat(effect, *definition, "direction"));
            out += QString("    // %1 - continuous volumetric-look cloud deck without visible raymarch slices\n"
                           "    float %2_windAngle = %9 * 0.01745329252;\n"
                           "    float2 %2_wind = float2(cos(%2_windAngle), sin(%2_windAngle));\n"
                           "    float %2_layerHeight = 1.8 + %8 * 3.2;\n"
                           "    float %2_viewZ = max(d.z, 0.035);\n"
                           "    float %2_cloudT = %2_layerHeight / %2_viewZ;\n"
                           "    float2 %2_world = d.xy * %2_cloudT + %2_wind * (t * %5 * 0.32);\n"
                           "    float3 %2_p = float3(%2_world * (%3 * 0.16), t * %5 * 0.018);\n"
                           "    float %2_broad = BO3BeginnerFbm3(%2_p * 0.62 + float3(1.7,-2.4,0.9));\n"
                           "    float %2_shape = BO3BeginnerFbm3(%2_p);\n"
                           "    float %2_detail = BO3BeginnerFbm3(%2_p * 2.15 + float3(4.3,1.2,-3.7));\n"
                           "    float %2_field = %2_broad * 0.34 + %2_shape * 0.78 - (1.0 - %2_detail) * 0.15;\n"
                           "    float %2_threshold = lerp(0.82, 0.39, saturate(%4));\n"
                           "    float %2_density = smoothstep(%2_threshold, %2_threshold + 0.13, %2_field);\n"
                           "    %2_density = %2_density * %2_density * (3.0 - 2.0 * %2_density);\n"
                           "    float %2_skyMask = smoothstep(0.014, 0.075, d.z);\n"
                           "    float3 %2_sunStep = float3(beginnerSunDir.xy * 0.24, beginnerSunDir.z * 0.11);\n"
                           "    float %2_sunField = BO3BeginnerFbm3(%2_p + %2_sunStep);\n"
                           "    float %2_lightThrough = saturate(0.48 + (%2_shape - %2_sunField) * 1.8);\n"
                           "    float %2_forward = pow(saturate(dot(d, beginnerSunDir)), 22.0) * beginnerDaylight;\n"
                           "    float %2_edge = smoothstep(0.04, 0.42, 1.0 - %2_density) * %2_forward;\n"
                           "    float %2_heightLight = saturate(0.52 + d.z * 0.55);\n"
                           "    float %2_light = saturate(0.20 + %2_lightThrough * 0.62 + %2_heightLight * 0.18 + %2_edge * 0.38);\n"
                           "    float3 %2_cloudColor = lerp(%10, %11, %2_light);\n"
                           "    %2_cloudColor *= %7 * lerp(0.62, 1.0, beginnerDaylight);\n"
                           "    float %2_alpha = saturate(%2_density * %6 * %2_skyMask);\n"
                           "    float %2_horizonAtmosphere = smoothstep(0.012, 0.12, d.z);\n"
                           "    %2_cloudColor = lerp(color, %2_cloudColor, %2_horizonAtmosphere);\n"
                           "    color = lerp(color, %2_cloudColor, %2_alpha);\n")
                .arg(definition->name)
                .arg(tag)
                .arg(scale)
                .arg(coverage)
                .arg(speed)
                .arg(opacity)
                .arg(brightness)
                .arg(height)
                .arg(direction)
                .arg(colorLiteral(shadowColor))
                .arg(colorLiteral(lightColor));
        }
        else if(effect.typeId == "sky_mountains" && project.target == Target::Sky)
        {
            const QColor nearColor = parameterColor(effect, *definition, "near_color");
            const QColor farColor = parameterColor(effect, *definition, "far_color");
            const QString height = floatLiteral(parameterFloat(effect, *definition, "height"));
            const QString roughness = floatLiteral(parameterFloat(effect, *definition, "roughness"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString softness = floatLiteral(parameterFloat(effect, *definition, "softness"));
            out += QString("    // %1 - three atmospheric mountain layers with broad mass + restrained ridge detail\n"
                           "    float2 %2_hd = normalize(d.xy + float2(1e-6, 0.0));\n"
                           "    float %2_farBroad = BO3BeginnerFbm3(float3(%2_hd * (%3 * 0.31), 1.35));\n"
                           "    float %2_farDetail = BO3BeginnerFbm3(float3(%2_hd * (%3 * 0.83) + float2(1.4,-2.1), 3.7));\n"
                           "    float %2_midBroad = BO3BeginnerFbm3(float3(%2_hd * (%3 * 0.43) + float2(-2.0,0.9), 5.2));\n"
                           "    float %2_midDetail = BO3BeginnerFbm3(float3(%2_hd * (%3 * 1.06) + float2(3.3,1.1), 7.5));\n"
                           "    float %2_nearBroad = BO3BeginnerFbm3(float3(%2_hd * (%3 * 0.56) + float2(0.4,2.7), 9.1));\n"
                           "    float %2_nearDetail = BO3BeginnerFbm3(float3(%2_hd * (%3 * 1.28) + float2(-4.2,1.5), 11.3));\n"
                           "    float %2_farRidge = pow(saturate(1.0 - abs(%2_farDetail * 2.0 - 1.0)), 2.6);\n"
                           "    float %2_midRidge = pow(saturate(1.0 - abs(%2_midDetail * 2.0 - 1.0)), 2.8);\n"
                           "    float %2_nearRidge = pow(saturate(1.0 - abs(%2_nearDetail * 2.0 - 1.0)), 3.0);\n"
                           "    float %2_farHeight = %4 * 0.55 + (%2_farBroad - 0.48) * (%5 * 0.34) + %2_farRidge * (%5 * 0.08);\n"
                           "    float %2_midHeight = %4 * 0.78 + (%2_midBroad - 0.47) * (%5 * 0.48) + %2_midRidge * (%5 * 0.11);\n"
                           "    float %2_nearHeight = %4 + (%2_nearBroad - 0.46) * (%5 * 0.62) + %2_nearRidge * (%5 * 0.14);\n"
                           "    // Estimate terrain slope in azimuth so the closest range receives actual directional sun shading instead of one flat silhouette color.\n"
                           "    const float %2_sideC = 0.9999280;\n"
                           "    const float %2_sideS = 0.0119997;\n"
                           "    float2 %2_hdL = float2(%2_hd.x * %2_sideC + %2_hd.y * %2_sideS, -%2_hd.x * %2_sideS + %2_hd.y * %2_sideC);\n"
                           "    float2 %2_hdR = float2(%2_hd.x * %2_sideC - %2_hd.y * %2_sideS,  %2_hd.x * %2_sideS + %2_hd.y * %2_sideC);\n"
                           "    float %2_nearL = %4 + (BO3BeginnerFbm3(float3(%2_hdL * (%3 * 0.56) + float2(0.4,2.7), 9.1)) - 0.46) * (%5 * 0.62);\n"
                           "    float %2_nearR = %4 + (BO3BeginnerFbm3(float3(%2_hdR * (%3 * 0.56) + float2(0.4,2.7), 9.1)) - 0.46) * (%5 * 0.62);\n"
                           "    float %2_slope = clamp((%2_nearR - %2_nearL) / 0.024, -6.0, 6.0);\n"
                           "    float2 %2_azTangent = float2(-%2_hd.y, %2_hd.x);\n"
                           "    float %2_sunAcross = dot(beginnerSunDir.xy, %2_azTangent);\n"
                           "    float2 %2_profileNormal = normalize(float2(-%2_slope, 1.0));\n"
                           "    float2 %2_profileLight = normalize(float2(%2_sunAcross, beginnerSunDir.z + 0.001));\n"
                           "    float %2_diffuse = saturate(dot(%2_profileNormal, %2_profileLight) * 0.5 + 0.5);\n"
                           "    float %2_farMask = 1.0 - smoothstep(%2_farHeight, %2_farHeight + %6 * 1.6, d.z);\n"
                           "    float %2_midMask = 1.0 - smoothstep(%2_midHeight, %2_midHeight + %6 * 1.25, d.z);\n"
                           "    float %2_nearMask = 1.0 - smoothstep(%2_nearHeight, %2_nearHeight + %6, d.z);\n"
                           "    float %2_haze = pow(saturate(1.0 - abs(d.z)), 5.0) * (0.18 + beginnerDaylight * 0.22);\n"
                           "    float3 %2_farColor = lerp(%7, color, saturate(0.42 + %2_haze * 0.58));\n"
                           "    float3 %2_midColor = lerp(%7, %8, 0.50) * lerp(0.76, 1.00, beginnerDaylight);\n"
                           "    float %2_nearLight = lerp(0.52, 1.02, %2_diffuse) * lerp(0.76, 1.0, beginnerDaylight);\n"
                           "    float3 %2_nearColor = %8 * %2_nearLight;\n"
                           "    color = lerp(color, %2_farColor, saturate(%2_farMask));\n"
                           "    color = lerp(color, %2_midColor, saturate(%2_midMask));\n"
                           "    color = lerp(color, %2_nearColor, saturate(%2_nearMask));\n")
                .arg(definition->name, tag, scale, height, roughness, softness, colorLiteral(farColor), colorLiteral(nearColor));
        }
        else if(effect.typeId == "sky_aurora" && project.target == Target::Sky)
        {
            const QColor colorA = parameterColor(effect, *definition, "color_a");
            const QColor colorB = parameterColor(effect, *definition, "color_b");
            const QString intensity = floatLiteral(parameterFloat(effect, *definition, "intensity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString azimuth = floatLiteral(parameterFloat(effect, *definition, "azimuth"));
            out += QString("    // %1 - compact layered aurora volume inspired by BO3 direction-space sky references\n"
                           "    float %2_az = %3 * 6.2831853;\n"
                           "    float2 %2_xy = BO3BeginnerRotate2(d.xy, -%2_az);\n"
                           "    float %2_upper = smoothstep(0.015,0.18,d.z) * (1.0 - smoothstep(0.86,1.0,d.z));\n"
                           "    float3 %2_auroraAccum = 0.0;\n"
                           "    float %2_auroraWeight = 0.0;\n"
                           "    [loop] for(int %2_i = 0; %2_i < 10; ++%2_i)\n"
                           "    {\n"
                           "        float %2_fi = float(%2_i);\n"
                           "        float %2_layer = (%2_fi + 0.5) * 0.10;\n"
                           "        float2 %2_ap = float2(%2_xy.x * %4 + %2_xy.y * 1.35, d.z * 3.25 + %2_xy.y * (%4 * 0.28));\n"
                           "        %2_ap += float2(%2_layer * 0.53 + sin(t * %5 * 0.21 + %2_fi) * 0.035, %2_layer * 0.19);\n"
                           "        float %2_n = BO3BeginnerAuroraNoise(%2_ap, t * %5 + %2_layer * 1.7);\n"
                           "        float %2_ribbon = pow(saturate(%2_n), 1.55) * %2_upper;\n"
                           "        float %2_fade = exp2(-%2_fi * 0.17) * smoothstep(0.0, 0.18, %2_layer);\n"
                           "        float %2_phase = 0.5 + 0.5 * sin(%2_ap.x * 0.82 + t * %5 * 0.70 + %2_layer * 5.0);\n"
                           "        float3 %2_layerColor = lerp(%6, %7, %2_phase);\n"
                           "        %2_auroraAccum += %2_layerColor * (%2_ribbon * %2_fade);\n"
                           "        %2_auroraWeight += %2_ribbon * %2_fade;\n"
                           "    }\n"
                           "    float %2_glow = pow(saturate(%2_auroraWeight * 0.35), 0.62);\n"
                           "    color += (%2_auroraAccum * 0.26 + lerp(%6,%7,0.35) * %2_glow * 0.22) * %8;\n")
                .arg(definition->name, tag, azimuth, scale, speed, colorLiteral(colorA), colorLiteral(colorB), intensity);
        }
        else if(effect.typeId == "sky_nebula" && project.target == Target::Sky)
        {
            const QColor colorA = parameterColor(effect, *definition, "color_a");
            const QColor colorB = parameterColor(effect, *definition, "color_b");
            const QString intensity = floatLiteral(parameterFloat(effect, *definition, "intensity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString drift = floatLiteral(parameterFloat(effect, *definition, "drift"));
            out += QString("    // %1 - direction-space folded fractal regions\n"
                           "    float3 %2_drift = float3(sin(t * 0.071), sin(t * 0.053 + 1.7), cos(t * 0.043 - 0.8)) * %3;\n"
                           "    float3 %2_np = d * %4 + %2_drift;\n"
                           "    float %2_f1 = BO3BeginnerNebulaField(%2_np + float3(0.8,-0.2,0.4), 0.7);\n"
                           "    float %2_f2 = BO3BeginnerNebulaField(%2_np.yzx * 1.13 + float3(-0.5,1.1,-0.7), 2.1);\n"
                           "    float %2_neb = max(%2_f1, %2_f2 * 0.84);\n"
                           "    float3 %2_nebColor = lerp(%5, %6, saturate(%2_f2 * 1.2));\n"
                           "    color += %2_nebColor * (%2_neb * %7);\n")
                .arg(definition->name, tag, drift, scale, colorLiteral(colorA), colorLiteral(colorB), intensity);
        }
        else if(effect.typeId == "sky_water" && project.target == Target::Sky)
        {
            // Direction reflection and water finishing are applied once around the
            // entire sky stack in generateSky(), so every sky effect participates
            // in the reflection instead of reflecting only one layer.
            out += QString("    // %1 - whole-sky still-water reflection; lower-direction mirroring adapted from the supplied Shadertoy reference\n")
                .arg(definition->name);
        }
        else if(effect.typeId == "grid_rings" && hasUv)
        {
            const QColor patternColor = parameterColor(effect, *definition, "color");
            const QString strength = floatLiteral(parameterFloat(effect, *definition, "strength"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString rings = floatLiteral(parameterFloat(effect, *definition, "rings"));
            if(project.target == Target::Material)
            {
                out += QString("    // %1 (seamless local 3D pattern)\n"
                               "    float3 %2_gridP = surfacePosition * (0.01 * %3);\n"
                               "    float3 %2_gridCell = min(frac(%2_gridP), 1.0 - frac(%2_gridP));\n"
                               "    float %2_grid = 1.0 - smoothstep(0.025, 0.085, min(%2_gridCell.x, min(%2_gridCell.y, %2_gridCell.z)));\n"
                               "    float %2_ringWave = 0.5 + 0.5 * cos(length(surfacePosition.xy) * (0.01 * %3) * 6.2831853);\n"
                               "    float %2_ring = smoothstep(0.82, 0.98, %2_ringWave);\n"
                               "    float %2_pattern = lerp(%2_grid, %2_ring, %4);\n"
                               "    color += %5 * (%2_pattern * %6);\n")
                    .arg(definition->name, tag, scale, rings, colorLiteral(patternColor), strength);
            }
            else
            {
                out += QString("    // %1\n"
                               "    float2 %2_gridP = uv * %3;\n"
                               "    float2 %2_gridCell = min(frac(%2_gridP), 1.0 - frac(%2_gridP));\n"
                               "    float %2_grid = 1.0 - smoothstep(0.025, 0.085, min(%2_gridCell.x, %2_gridCell.y));\n"
                               "    float %2_ringWave = 0.5 + 0.5 * cos(length(uv - 0.5) * %3 * 6.2831853);\n"
                               "    float %2_ring = smoothstep(0.82, 0.98, %2_ringWave);\n"
                               "    float %2_pattern = lerp(%2_grid, %2_ring, %4);\n"
                               "    color += %5 * (%2_pattern * %6);\n")
                    .arg(definition->name, tag, scale, rings, colorLiteral(patternColor), strength);
            }
        }
    }
    return out;
}

bool projectUsesEffect(const Project& project, const QString& id)
{
    for(const Effect& effect : project.effects)
        if(effect.enabled && effect.typeId == id) return true;
    return false;
}

const Effect* firstEnabledEffect(const Project& project, const QString& id)
{
    for(const Effect& effect : project.effects)
        if(effect.enabled && effect.typeId == id) return &effect;
    return nullptr;
}

QString optionalHelpers(const Project& project)
{
    QString out;
    const bool needsHash21 =
        (project.target != Target::Material && projectUsesEffect(project, "noise")) ||
        projectUsesEffect(project, "film_grain") ||
        projectUsesEffect(project, "paint_strokes") ||
        projectUsesEffect(project, "red_paint_splatter") ||
        projectUsesEffect(project, "vhs_tape") ||
        projectUsesEffect(project, "vhs_dropouts");
    const bool needsHash31 =
        (project.target == Target::Material && projectUsesEffect(project, "noise")) ||
        projectUsesEffect(project, "dissolve") ||
        projectUsesEffect(project, "sky_clouds") ||
        projectUsesEffect(project, "sky_realistic_clouds") ||
        projectUsesEffect(project, "sky_mountains") ||
        projectUsesEffect(project, "sky_stars") ||
        projectUsesEffect(project, "sky_nebula");
    const bool needsHash11 = projectUsesEffect(project, "flicker");

    if(needsHash21)
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerHash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
)HLSL");
    }

    if(needsHash31)
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerHash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
)HLSL");
    }

    const bool needsSceneDepth = project.target == Target::PostFx &&
                                 (projectUsesEffect(project, "cartoon_outlines") ||
                                  projectUsesEffect(project, "ambient_occlusion") ||
                                  projectUsesEffect(project, "depth_fog"));
    if(needsSceneDepth)
    {
        out += QStringLiteral(R"HLSL(
static const float BO3_BEGINNER_FLOATZ_DEPTHHACK_SPLIT = 63.0 / 64.0;

float BO3BeginnerSampleRawDepthPoint(float2 sampleUv)
{
    float2 depthSize = max(PostFx_GetRenderTargetSize().xy, float2(1.0, 1.0));
    float2 clampedUv = saturate(sampleUv);
    int2 depthPixel = int2(clamp(floor(clampedUv * depthSize), float2(0.0, 0.0), depthSize - 1.0));
    return DepthSampler.Load(int3(depthPixel, 0)).r;
}

float BO3BeginnerLinearDepth(float rawDepth)
{
    return max(zNear.x, 0.001) / FloatZ_Process(rawDepth);
}

float BO3BeginnerSampleWorldDepth(float2 sampleUv)
{
    float rawDepth = BO3BeginnerSampleRawDepthPoint(sampleUv);
    if(rawDepth >= BO3_BEGINNER_FLOATZ_DEPTHHACK_SPLIT)
        return -1.0;
    return BO3BeginnerLinearDepth(rawDepth);
}

float BO3BeginnerDepthControlToWorld(float control)
{
    // Beginner-facing 0..1 distance control mapped over the useful BO3
    // Float-Z scene range. Exponential spacing gives much finer control nearby.
    return exp2(lerp(3.0, 13.6, saturate(control)));
}

float BO3BeginnerSSAOHash12(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * float3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.x + p3.y) * p3.z);
}

float BO3BeginnerSSAOPair(float centerDepth, float sampleA, float sampleB, float depthBias, float depthRange)
{
    float validA = step(0.0001, sampleA);
    float validB = step(0.0001, sampleB);
    float deltaA = centerDepth - sampleA;
    float deltaB = centerDepth - sampleB;
    float frontA = saturate((deltaA - depthBias) / max(depthRange, 0.0001)) * validA;
    float frontB = saturate((deltaB - depthBias) / max(depthRange, 0.0001)) * validB;
    float rangeMaskA = 1.0 - smoothstep(depthRange * 1.10, depthRange * 3.20, abs(deltaA));
    float rangeMaskB = 1.0 - smoothstep(depthRange * 1.10, depthRange * 3.20, abs(deltaB));
    frontA *= rangeMaskA;
    frontB *= rangeMaskB;
    float paired = sqrt(max(frontA * frontB, 0.0));
    float strongest = max(frontA, frontB);
    float pairBalance = 1.0 - smoothstep(depthRange * 0.35, depthRange * 2.20, abs(deltaA - deltaB));
    float meanDepth = (sampleA + sampleB) * 0.5;
    float curvature = saturate((centerDepth - meanDepth - depthBias) / max(depthRange * 0.72, 0.0001)) * validA * validB;
    return saturate(paired * 0.72 + strongest * pairBalance * 0.18 + curvature * 0.46);
}
)HLSL");
    }

    if(needsHash11)
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerHash11(float p)
{
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}
)HLSL");
    }
    if(projectUsesEffect(project, "film_grain"))
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerGrainHash(float2 p)
{
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x * p.y);
}

float BO3BeginnerGrainLayer(float2 uv, float grainSize, float angle, float phase, float speed)
{
    float s = sin(angle);
    float c = cos(angle);
    float2 pixelPosition = uv * PostFx_GetRenderTargetSize().xy;
    float2 rotated = float2(
        c * pixelPosition.x - s * pixelPosition.y,
        s * pixelPosition.x + c * pixelPosition.y);
    rotated /= max(grainSize, 0.001);
    float framePhase = floor(GetTime() * 30.0 * speed + phase);
    float2 frameOffset = float2(framePhase, framePhase * 1.37);
    return BO3BeginnerGrainHash(floor(rotated) + frameOffset) * 2.0 - 1.0;
}
)HLSL");
    }
    if(projectUsesEffect(project, "psx_dithering"))
    {
        out += QStringLiteral(R"HLSL(
static const float4x4 BO3BeginnerPsxDither = float4x4(
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);
)HLSL");
    }
    const bool needsSkyNoise =
        projectUsesEffect(project, "sky_clouds") ||
        projectUsesEffect(project, "sky_realistic_clouds") ||
        projectUsesEffect(project, "sky_mountains") ||
        projectUsesEffect(project, "sky_nebula");
    if(needsSkyNoise)
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerNoise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = BO3BeginnerHash31(i + float3(0,0,0));
    float n100 = BO3BeginnerHash31(i + float3(1,0,0));
    float n010 = BO3BeginnerHash31(i + float3(0,1,0));
    float n110 = BO3BeginnerHash31(i + float3(1,1,0));
    float n001 = BO3BeginnerHash31(i + float3(0,0,1));
    float n101 = BO3BeginnerHash31(i + float3(1,0,1));
    float n011 = BO3BeginnerHash31(i + float3(0,1,1));
    float n111 = BO3BeginnerHash31(i + float3(1,1,1));
    float n00 = lerp(n000, n100, f.x);
    float n10 = lerp(n010, n110, f.x);
    float n01 = lerp(n001, n101, f.x);
    float n11 = lerp(n011, n111, f.x);
    return lerp(lerp(n00, n10, f.y), lerp(n01, n11, f.y), f.z);
}

float BO3BeginnerFbm3(float3 p)
{
    // Keep this as a real loop instead of asking FXC to unroll a large nested
    // cloud expression every time a Beginner slider moves. Four octaves retain
    // the broad/detail breakup while compiling much faster than the old
    // five-octave fully-unrolled path.
    float sum = 0.0;
    float amp = 0.56;
    [loop] for(int i = 0; i < 4; ++i)
    {
        sum += BO3BeginnerNoise3(p) * amp;
        p = p.yzx * 2.07 + float3(1.7, 3.1, 2.4);
        amp *= 0.49;
    }
    return sum;
}
)HLSL");
    }

    if(projectUsesEffect(project, "sky_aurora"))
    {
        out += QStringLiteral(R"HLSL(
float2 BO3BeginnerRotate2(float2 p, float a)
{
    float c = cos(a), s = sin(a);
    return float2(c*p.x - s*p.y, s*p.x + c*p.y);
}

float BO3BeginnerTri(float x)
{
    return abs(frac(x) - 0.5);
}

float2 BO3BeginnerTri2(float2 p)
{
    return float2(BO3BeginnerTri(p.x + BO3BeginnerTri(p.y)),
                  BO3BeginnerTri(p.y + BO3BeginnerTri(p.x)));
}

float BO3BeginnerAuroraNoise(float2 p, float time)
{
    float sum = 0.0;
    float amp = 0.62;
    float2 q = p;
    [unroll] for(int i = 0; i < 5; ++i)
    {
        float2 warp = BO3BeginnerTri2(q * 1.73) - 0.5;
        q += BO3BeginnerRotate2(warp, time * (0.10 + i * 0.025)) * (0.72 / (1.0 + i));
        sum += (1.0 - saturate(BO3BeginnerTri(q.x + BO3BeginnerTri(q.y)) * 2.0)) * amp;
        q = BO3BeginnerRotate2(q * 1.31 + float2(0.37, -0.22), -0.31);
        amp *= 0.48;
    }
    return saturate(sum * 0.82);
}
)HLSL");
    }

    if(projectUsesEffect(project, "sky_nebula"))
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerNebulaField(float3 p, float seed)
{
    float accum = 0.0;
    float prev = 0.0;
    float weightSum = 0.0;
    [unroll] for(int i = 0; i < 12; ++i)
    {
        float mag = max(dot(p,p), 1e-4);
        p = abs(p) / mag + float3(-0.47, -0.39, -1.34 + seed * 0.03);
        float w = exp(-float(i) / 5.8);
        float delta = abs(mag - prev);
        accum += w * exp(-6.4 * pow(max(delta,0.0), 2.1));
        weightSum += w;
        prev = mag;
    }
    return saturate(4.2 * accum / max(weightSum,1e-4) - 0.55);
}
)HLSL");
    }
    return out;
}

QString effectStackMarker(const Project& project)
{
    QStringList names;
    for(const Effect& effect : project.effects)
    {
        if(!effect.enabled) continue;
        const EffectDefinition* definition = effectDefinition(effect.typeId);
        if(definition && supportsTarget(*definition, project.target)) names << definition->name;
    }
    return names.isEmpty() ? QStringLiteral("None") : names.join(" -> ");
}

QString generatePostFx(const Project& project)
{
    const QString helpers = optionalHelpers(project);
    const QString effects = commonEffectCode(project, true, true);
    return QStringLiteral(R"HLSL(// BO3 Shader Studio - Beginner Shader Builder
// BO3_BEGINNER_PROJECT: 1
// BO3_BEGINNER_TARGET: POSTFX
// BO3_BEGINNER_EFFECT_STACK: %1
#include "postfx/postfx_common.h"

Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);
SamplerState bilinearClampler : register(s1);

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
    PostFx_GenerateFullscreenQuad(vertex.position, vertex.texcoord, instance, output.position, output.texcoord);
    return output;
}
%2
float4 ps_main(PS_INPUT input) : SV_Target
{
    float2 uv = saturate(input.texcoord);
    float t = GetTime();
    float beginnerVertical = uv.y;
    float3 color = PostFx_NormalizeColor(frameBuffer.Sample(bilinearClampler, uv).rgb);
%3
    color = max(color, 0.0);
    return float4(PostFx_DenormalizeColor(color), 1.0);
}
)HLSL").arg(effectStackMarker(project), helpers, effects);
}

QString generateMaterial(const Project& project)
{
    const QColor base = settingColor(project, "baseColor", QColor("#2F78D0"));
    const QString helpers = optionalHelpers(project);
    const QString effects = commonEffectCode(project, true, true);
    return QStringLiteral(R"HLSL(// BO3 Shader Studio - Beginner Shader Builder
// BO3_BEGINNER_PROJECT: 1
// BO3_BEGINNER_TARGET: MATERIAL
// BO3_BEGINNER_EFFECT_STACK: %1
// BO3_PREVIEWER_MATERIAL_SURFACE: EMISSIVE

#include "lib/globals.hlsl"
#include "lib/transform.hlsl"
#include "lib/vertdecl_vertex.hlsl"
#include "lib/vertdecl_vertex_tangentspace.hlsl"
#include "lib/gpu_skin.hlsl"
#include "lib/gbuffer.hlsl"

struct BeginnerMaterialInput
{
    float4 position      : SV_POSITION;
    float4 texCoords     : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 normal        : TEXCOORD2;
    float4 tangent       : TEXCOORD3;
    float4 biTangent     : TEXCOORD4;
    float4 localPosition : TEXCOORD5;
};

BeginnerMaterialInput vs_main(const GBufferVertexInput vertex, const uint instance : INSTANCE_SEMANTIC)
{
    BeginnerMaterialInput output;
    float3 position = vertex.position;
    float3 normal = Vertex_DecodeNormal(vertex.normal);
    float3 tangent = Vertex_DecodeNormal(vertex.tangent.xyz);
    GPUSkin_SkinVertex(position, normal, tangent, vertex.weights, vertex.indices, instance);
    const float3 localPosition = position;
    position = Transform_PositionToWorld(position, instance);
    normal = normalize(Transform_NormalToWorld(normal, instance));
    tangent = normalize(Transform_NormalToWorld(tangent, instance));
    output.position = Transform_OffsetToClip(position);
    output.texCoords = float4(vertex.texCoords, 0.0, 0.0);
    output.worldPosition = float4(position, 1.0);
    output.normal = float4(normal, 0.0);
    output.tangent = float4(tangent, 0.0);
    output.biTangent = float4(Vertex_CalculateBiNormal(normal, tangent, Vertex_DecodeBiNormalSign(vertex.tangent.w)), 0.0);
    output.localPosition = float4(localPosition, 1.0);
    return output;
}
%2
float4 ps_main(const BeginnerMaterialInput input) : SV_TARGET0
{
    float2 uv = input.texCoords.xy;
    float3 surfacePosition = input.localPosition.xyz;
    float3 surfaceNormal = normalize(input.normal.xyz);
    float3 surfaceViewDir = normalize(Transform_GetCameraWorldPosition() - input.worldPosition.xyz);
    float t = GetTime();
    float3 color = %3;
%4
    // Keep HDR/emissive values above 1.0. BO3's forward custom-material path
    // accepts them and the preview tone mapper can show the resulting glow.
    return float4(max(color, 0.0), 1.0);
}
)HLSL").arg(effectStackMarker(project), helpers, colorLiteral(base), effects);
}

QString generateSky(const Project& project)
{
    const QColor zenith = settingColor(project, "zenithColor", QColor("#102E68"));
    const QColor horizonColor = settingColor(project, "horizonColor", QColor("#E17658"));
    const QColor ground = settingColor(project, "groundColor", QColor("#060B18"));
    const QString helpers = optionalHelpers(project);
    const QString effects = commonEffectCode(project, true, false);

    double sunTime = 14.0;
    double sunAzimuth = 0.12;
    double sunArcHeight = 0.86;
    if(const Effect* sun = firstEnabledEffect(project, "sky_sun"))
    {
        if(const EffectDefinition* def = effectDefinition("sky_sun"))
        {
            sunTime = parameterFloat(*sun, *def, "time_of_day");
            sunAzimuth = parameterFloat(*sun, *def, "azimuth");
            sunArcHeight = parameterFloat(*sun, *def, "height");
        }
    }

    QString waterDirectionPrelude = QStringLiteral(
        "    float3 rawSkyDirection = normalize(input.skyDirection.xyz);\n"
        "    float beginnerWaterMask = 0.0;\n"
        "    float3 d = rawSkyDirection;\n");
    QString waterFinalize;
    if(const Effect* water = firstEnabledEffect(project, "sky_water"))
    {
        if(const EffectDefinition* def = effectDefinition("sky_water"))
        {
            const QColor tint = parameterColor(*water, *def, "tint");
            const QString reflection = floatLiteral(parameterFloat(*water, *def, "reflection"));
            const QString ripple = floatLiteral(parameterFloat(*water, *def, "ripple"));
            const QString scale = floatLiteral(parameterFloat(*water, *def, "scale"));
            const QString speed = floatLiteral(parameterFloat(*water, *def, "speed"));
            const QString horizonBlend = floatLiteral(parameterFloat(*water, *def, "horizon"));
            waterDirectionPrelude = QString(
                "    float3 rawSkyDirection = normalize(input.skyDirection.xyz);\n"
                "    float beginnerWaterMask = 1.0 - smoothstep(-%1, %1, rawSkyDirection.z);\n"
                "    float3 beginnerReflectedDirection = rawSkyDirection;\n"
                "    beginnerReflectedDirection.z = abs(beginnerReflectedDirection.z);\n"
                "    float2 beginnerWaterAxis = normalize(rawSkyDirection.xy + float2(1e-5,0.0));\n"
                "    float beginnerWaterPhaseA = dot(beginnerWaterAxis, float2(0.83,0.56)) * %2 + t * %3 * 1.7;\n"
                "    float beginnerWaterPhaseB = dot(beginnerWaterAxis, float2(-0.42,0.91)) * (%2 * 1.73) - t * %3 * 1.1;\n"
                "    float2 beginnerWaterRipple = float2(sin(beginnerWaterPhaseA), cos(beginnerWaterPhaseB)) * (%4 * 0.0065);\n"
                "    beginnerWaterRipple *= smoothstep(0.0,0.22,abs(rawSkyDirection.z));\n"
                "    beginnerReflectedDirection.xy += beginnerWaterRipple;\n"
                "    beginnerReflectedDirection = normalize(beginnerReflectedDirection);\n"
                "    float3 d = normalize(lerp(rawSkyDirection, beginnerReflectedDirection, beginnerWaterMask));\n")
                .arg(horizonBlend, scale, speed, ripple);
            waterFinalize = QString(
                "    // Still-water reflection: all sky effects above were evaluated with a mirrored lower direction.\n"
                "    float beginnerFresnel = pow(saturate(1.0 - abs(rawSkyDirection.z)), 3.0);\n"
                "    float3 beginnerWaterColor = color * %1;\n"
                "    beginnerWaterColor = lerp(beginnerWaterColor, beginnerWaterColor * %2, 0.28 + beginnerFresnel * 0.18);\n"
                "    color = lerp(color, beginnerWaterColor, beginnerWaterMask);\n")
                .arg(reflection, colorLiteral(tint));
        }
    }

    const QString sharedSun = QString(
        "    // Shared time-of-day sun direction. Clouds and atmosphere use the same light path.\n"
        "    float beginnerTimeOfDay = %1;\n"
        "    float beginnerSolarPhase = (beginnerTimeOfDay - 6.0) * (6.28318530718 / 24.0);\n"
        "    float beginnerSunElevation = sin(beginnerSolarPhase) * %2;\n"
        "    float beginnerSunAzimuth = %3 * 6.28318530718 + cos(beginnerSolarPhase) * 1.15;\n"
        "    float beginnerSunHorizontal = sqrt(max(1.0 - beginnerSunElevation * beginnerSunElevation, 0.0));\n"
        "    float3 beginnerSunDir = normalize(float3(cos(beginnerSunAzimuth) * beginnerSunHorizontal, sin(beginnerSunAzimuth) * beginnerSunHorizontal, beginnerSunElevation));\n"
        "    float beginnerDaylight = smoothstep(-0.10, 0.075, beginnerSunElevation);\n")
        .arg(floatLiteral(sunTime), floatLiteral(sunArcHeight), floatLiteral(sunAzimuth));

    return QStringLiteral(R"HLSL(// BO3 Shader Studio - Beginner Shader Builder
// BO3_BEGINNER_PROJECT: 1
// BO3_BEGINNER_TARGET: SKY
// BO3_BEGINNER_EFFECT_STACK: %1
// BO3_PREVIEWER_SKY_SOURCE: SELF_CAMERA
// BO3_PREVIEWER_SKY_SELF_CAMERA

cbuffer PerSceneConsts : register(b1)
{
    float3 sunFogDir : packoffset(c38);
    float4 sunFogColor : packoffset(c39);
    float2 sunFog : packoffset(c40);
    float4 renderTargetSize : packoffset(c44);
    float4 skyMxR : packoffset(c51);
    float4 skyMxG : packoffset(c52);
    float4 skyMxB : packoffset(c53);
    float4 sunMxR : packoffset(c54);
    float4 sunMxG : packoffset(c55);
    float4 sunMxB : packoffset(c56);
    float4 skyRotationTransition : packoffset(c57);
    float4 gameTime : packoffset(c69);
    float4 relHDRExposure : packoffset(c85);
};

struct PixelShaderInput
{
    float4 position : SV_POSITION0;
    float4 skyDirection : TEXCOORD0;
    float4 fogDirection : TEXCOORD1;
};

// No authored vs_main on purpose: the BO3 Sky package adapter selects the
// proven stock vs_sky stage, which supplies the real engine skyDirection.
%2
float4 ps_main(const PixelShaderInput input) : SV_TARGET0
{
    float t = gameTime.w;
%3%4
    float horizon = saturate(1.0 - abs(d.z));
    float up = saturate(d.z * 0.5 + 0.5);
    float beginnerVertical = up;
    float3 color = lerp(%5, %6, smoothstep(0.0, 0.62, up));
    color = lerp(color, %7, pow(horizon, 5.0) * 0.72);
%8%9
    return float4(clamp(color, float3(0.0, 0.0, 0.0), float3(65024.0, 65024.0, 65024.0)), 1.0);
}
)HLSL").arg(effectStackMarker(project), helpers, waterDirectionPrelude, sharedSun,
             colorLiteral(ground), colorLiteral(zenith), colorLiteral(horizonColor), effects, waterFinalize);
}

} // namespace

QString targetId(Target target)
{
    switch(target)
    {
        case Target::Material: return "material";
        case Target::Sky: return "sky";
        default: return "postfx";
    }
}

QString targetName(Target target)
{
    switch(target)
    {
        case Target::Material: return "Object / Material";
        case Target::Sky: return "Sky / Environment";
        default: return "Screen Effect";
    }
}

QString targetDescription(Target target)
{
    switch(target)
    {
        case Target::Material:
            return "Give a BO3 model or surface a custom look. Preview it on a sphere, cube, plane, or your own model.";
        case Target::Sky:
            return "Create the colors and animated atmosphere around the player. Drag in the preview to look around.";
        default:
            return "Change how the game screen looks. Use Preview Image to test the effect on any screenshot.";
    }
}

bool targetFromId(const QString& id, Target& target)
{
    const QString lower = id.trimmed().toLower();
    if(lower == "postfx" || lower == "screen") { target = Target::PostFx; return true; }
    if(lower == "material" || lower == "surface") { target = Target::Material; return true; }
    if(lower == "sky" || lower == "environment") { target = Target::Sky; return true; }
    return false;
}

const QVector<EffectDefinition>& effectDefinitions()
{
    static const QVector<EffectDefinition> definitions = {
        EffectDef("tint", "Color Tint", "Blend the shader toward a chosen color while keeping the original detail.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {ColorParam("color", "Color", "Tint color.", "#73A7FF"),
                   FloatParam("amount", "Strength", "How strongly the tint affects the result.", 0.0, 1.0, 0.01, 0.35)}),
        EffectDef("brightness", "Brightness", "Make the result brighter or darker.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Amount", "Negative values darken; positive values brighten.", -1.0, 1.0, 0.01, 0.08)}),
        EffectDef("contrast", "Contrast", "Increase or soften the difference between dark and bright areas.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Contrast", "1.0 keeps the original contrast.", 0.0, 2.5, 0.01, 1.15)}),
        EffectDef("saturation", "Saturation", "Control how colorful the result is.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Saturation", "0 is monochrome, 1 is original color, above 1 is more colorful.", 0.0, 2.5, 0.01, 1.15)}),
        EffectDef("grayscale", "Black & White", "Blend the shader toward grayscale.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Strength", "0 keeps color; 1 is fully black and white.", 0.0, 1.0, 0.01, 1.0)}),
        EffectDef("invert", "Invert Colors", "Invert the current colors, with adjustable strength.", "Color & Look",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Strength", "0 is unchanged; 1 is fully inverted.", 0.0, 1.0, 0.01, 1.0)}),

        EffectDef("vignette", "Vignette", "Darken the edges of a screen effect while keeping the center clear.", "Atmosphere",
                  {Target::PostFx},
                  {FloatParam("strength", "Strength", "How strongly the edges darken.", 0.0, 1.0, 0.01, 0.45),
                   FloatParam("size", "Size", "How much of the center remains clear.", 0.10, 0.95, 0.01, 0.70),
                   FloatParam("softness", "Softness", "Width of the edge transition.", 0.05, 1.0, 0.01, 0.40)}),
        EffectDef("noise", "Procedural Noise", "Add animated procedural noise without needing a texture image.", "Atmosphere",
                  {Target::PostFx, Target::Material},
                  {FloatParam("amount", "Strength", "Noise intensity.", 0.0, 0.75, 0.005, 0.05),
                   FloatParam("scale", "Scale", "How fine or coarse the noise pattern is.", 2.0, 1200.0, 1.0, 320.0),
                   FloatParam("speed", "Speed", "How quickly a new noise pattern appears.", 0.0, 4.0, 0.01, 0.35)}),
        EffectDef("film_grain", "Film Grain", "Layer fine, medium and coarse moving grain that follows the photographed image instead of looking like digital stripes.", "Atmosphere",
                  {Target::PostFx},
                  {FloatParam("amount", "Strength", "How strongly the grain modulates the photographed image.", 0.0, 2.0, 0.01, 0.28),
                   FloatParam("grain_size", "Grain Size", "Approximate grain particle size in screen pixels.", 0.55, 4.0, 0.05, 1.35),
                   FloatParam("speed", "Speed", "How quickly new film-grain frames appear.", 0.0, 3.0, 0.01, 1.0)}),
        EffectDef("cartoon_outlines", "Cartoon Outlines", "Draw clean cel-style line work from point-sampled BO3 Float-Z silhouettes, with optional image-detail ink and subtle toon shading.", "Depth & Scene",
                  {Target::PostFx},
                  {ColorParam("color", "Outline Color", "Color of the cartoon line work.", "#090909"),
                   FloatParam("strength", "Outline Strength", "How strongly the detected lines are drawn over the scene.", 0.0, 1.0, 0.01, 0.78),
                   FloatParam("thickness", "Line Width", "Width of the diagonal depth samples in screen pixels.", 0.5, 6.0, 0.1, 1.5),
                   FloatParam("depth_threshold", "Depth Threshold", "Higher values require a stronger Float-Z silhouette change before drawing a line.", 0.5, 12.0, 0.1, 5.0),
                   FloatParam("detail_edges", "Detail Edges", "Add line detail from scene luminance when depth alone is not enough.", 0.0, 1.0, 0.01, 0.14),
                   FloatParam("cel_amount", "Cel Shading", "How much luminance banding is mixed into the original scene. Zero keeps only the outlines.", 0.0, 1.0, 0.01, 0.08),
                   FloatParam("levels", "Toon Levels", "Number of brightness bands used when Cel Shading is above zero.", 2.0, 12.0, 1.0, 6.0)}),
        EffectDef("ambient_occlusion", "Ambient Occlusion", "Add depth-only screen-space contact and corner shading using twelve BO3 Float-Z samples arranged in opposing spiral pairs.", "Depth & Scene",
                  {Target::PostFx},
                  {FloatParam("amount", "Strength", "How strongly contact and corner shading is applied.", 0.0, 1.0, 0.01, 0.52),
                   FloatParam("radius", "Radius", "Maximum screen-space sampling radius before distance scaling.", 1.0, 14.0, 0.1, 6.0),
                   FloatParam("bias", "Bias", "Reject shallow depth differences and detached silhouettes that should not cast AO.", 0.0, 1.0, 0.01, 0.10)}),
        EffectDef("depth_fog", "Depth Fog", "Fade distant world geometry using linearized BO3 Float-Z distance instead of the raw depth texture.", "Depth & Scene",
                  {Target::PostFx},
                  {ColorParam("color", "Fog Color", "Color of the depth fog.", "#7CA2D9"),
                   FloatParam("start", "Near Distance", "Where fog begins across the useful BO3 scene-distance range.", 0.0, 1.0, 0.01, 0.34),
                   FloatParam("end", "Far Distance", "Where fog reaches full strength across the useful BO3 scene-distance range.", 0.0, 1.0, 0.01, 0.76),
                   FloatParam("falloff", "Distance Curve", "Lower values fill sooner; higher values keep fog concentrated farther away.", 0.25, 3.0, 0.05, 1.0),
                   FloatParam("strength", "Strength", "Maximum amount of fog applied.", 0.0, 1.0, 0.01, 0.65)}),
        EffectDef("luminance_tint", "Luminance Tint", "Color shadows and highlights differently based on scene brightness.", "Depth & Scene",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {ColorParam("shadow_color", "Shadow Color", "Color used in darker areas.", "#4F65B4"),
                   ColorParam("highlight_color", "Highlight Color", "Color used in brighter areas.", "#FFD280"),
                   FloatParam("strength", "Strength", "How strongly brightness controls the tint.", 0.0, 1.0, 0.01, 0.35),
                   FloatParam("contrast", "Contrast", "How sharply the tint changes between dark and bright.", 0.5, 4.0, 0.05, 1.6)}),

        EffectDef("scanlines", "Scanlines", "Add animated horizontal lines for CRT, hologram, visor and monitor looks.", "Retro & Display",
                  {Target::PostFx, Target::Material},
                  {FloatParam("amount", "Strength", "How dark the scanlines become.", 0.0, 0.75, 0.01, 0.12),
                   FloatParam("density", "Density", "Number of line cycles across the surface.", 8.0, 800.0, 1.0, 160.0),
                   FloatParam("speed", "Speed", "How quickly the line pattern moves.", -4.0, 4.0, 0.01, 0.25)}),
        EffectDef("chromatic_aberration", "Chromatic Aberration", "Separate the red and blue channels near edges for a lens or glitch look.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("amount", "Channel Offset", "How far the red and blue channels separate.", 0.0, 0.025, 0.0005, 0.004),
                   FloatParam("strength", "Strength", "How much of the channel separation is added.", 0.0, 1.0, 0.01, 0.65)}),
        EffectDef("posterize", "Posterize", "Reduce the image into clean color bands for comic, stylized and PSX-like looks.", "Retro & Display",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("levels", "Levels", "How many distinct color bands remain.", 2.0, 16.0, 1.0, 5.0),
                   FloatParam("strength", "Strength", "Blend amount between the original and posterized result.", 0.0, 1.0, 0.01, 1.0)}),
        EffectDef("fisheye", "Fisheye Lens", "Use a signed, corner-normalized radial lens curve that can bend outward or reverse inward without folding the frame.", "Movement & Distortion",
                  {Target::PostFx},
                  {FloatParam("amount", "Curvature", "Positive values create classic fisheye; negative values reverse the lens.", -10.0, 10.0, 0.05, 1.0),
                   FloatParam("zoom", "Frame Fit", "Compensate the frame scale after lens distortion.", 0.70, 1.30, 0.01, 1.0),
                   FloatParam("strength", "Strength", "Blend between the original and fisheye image.", 0.0, 1.0, 0.01, 1.0)}),

        EffectDef("psx_dithering", "PSX Dithering", "Add ordered 4x4 screen dithering with optional low color precision for a classic console look.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("strength", "Strength", "How strongly the dithered image replaces the original.", 0.0, 1.0, 0.01, 1.0),
                   FloatParam("color_precision", "Low Color Precision", "0 keeps full color precision; 1 quantizes toward a 5-bit-per-channel look.", 0.0, 1.0, 0.01, 0.65)}),
        EffectDef("luminance_sharpness", "Luminance Sharpness", "Sharpen local luminance detail while scaling RGB together, reducing colored halos around edges.", "Color & Look",
                  {Target::PostFx},
                  {FloatParam("amount", "Amount", "Strength of the luminance detail boost.", 0.0, 8.0, 0.05, 1.35),
                   FloatParam("radius", "Radius", "Sampling radius in screen pixels.", 0.5, 3.0, 0.05, 1.0),
                   FloatParam("threshold", "Detail Threshold", "Ignore tiny local luminance ranges so flat areas remain clean.", 0.0005, 0.05, 0.0005, 0.003)}),
        EffectDef("sharpness", "Sharpness", "Sharpen real image detail with an edge-aware 8-neighbor filter instead of a simple brightness boost.", "Color & Look",
                  {Target::PostFx},
                  {FloatParam("amount", "Amount", "Strength of the local detail boost.", 0.0, 3.0, 0.01, 0.65),
                   FloatParam("threshold", "Edge Threshold", "Ignore very small changes so flat areas stay clean.", 0.0, 1.0, 0.005, 0.055),
                   FloatParam("radius", "Radius", "Sampling radius in screen pixels.", 0.5, 3.0, 0.05, 1.0)}),
        EffectDef("pixel_resolution", "Pixel Resolution", "Lower the virtual screen resolution while preserving the full output size.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("width", "Virtual Width", "Approximate horizontal pixel count used for the low-resolution image.", 80.0, 1920.0, 1.0, 320.0),
                   FloatParam("strength", "Strength", "Blend between the full-resolution and pixel-resolution image.", 0.0, 1.0, 0.01, 1.0)}),
        EffectDef("vhs_tape", "VHS Tape", "Add row jitter, tracking tears and analog color separation without turning the whole picture into random static.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("strength", "Strength", "Overall amount of the VHS treatment.", 0.0, 1.0, 0.01, 0.65),
                   FloatParam("jitter", "Horizontal Jitter", "How strongly tape-line instability shifts the image.", 0.0, 3.0, 0.01, 0.85),
                   FloatParam("chroma", "Color Bleed", "Horizontal red/blue channel separation.", 0.0, 3.0, 0.01, 0.75),
                   FloatParam("tracking", "Tracking Tear", "How strongly occasional horizontal tears distort the image.", 0.0, 2.0, 0.01, 0.70),
                   FloatParam("speed", "Tape Speed", "Animation speed of the analog instability.", 0.05, 3.0, 0.01, 1.0)}),
        EffectDef("vhs_dropouts", "VHS Dropouts", "Add intermittent damaged-tape rows, image shifts and static bursts.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("strength", "Strength", "How visible the damaged tape rows become.", 0.0, 1.0, 0.01, 0.55),
                   FloatParam("density", "Band Height", "Approximate height of dropout bands in pixels.", 2.0, 40.0, 1.0, 9.0),
                   FloatParam("shift", "Horizontal Shift", "How far damaged rows can pull sideways.", 0.0, 2.0, 0.01, 0.70)}),

        EffectDef("pulse", "Animated Pulse", "Rhythmically brighten and dim the result.", "Animation",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Amount", "Brightness swing around the original value.", 0.0, 1.0, 0.01, 0.15),
                   FloatParam("speed", "Speed", "Pulses per second.", 0.05, 5.0, 0.01, 0.75)}),
        EffectDef("flicker", "Random Flicker", "Add irregular animated brightness changes for damaged screens, energy and horror effects.", "Animation",
                  {Target::PostFx, Target::Material, Target::Sky},
                  {FloatParam("amount", "Strength", "How strongly brightness flickers.", 0.0, 0.80, 0.01, 0.12),
                   FloatParam("speed", "Speed", "How frequently the random value changes.", 0.05, 8.0, 0.01, 1.2)}),

        EffectDef("uv_scroll", "Screen Scroll", "Slide the game image horizontally and vertically over time.", "Movement & Distortion",
                  {Target::PostFx},
                  {FloatParam("speed_x", "Horizontal Speed", "Positive moves right; negative moves left.", -1.0, 1.0, 0.01, 0.06),
                   FloatParam("speed_y", "Vertical Speed", "Positive moves down; negative moves up.", -1.0, 1.0, 0.01, 0.0),
                   FloatParam("amount", "Strength", "Blend between the original scene and the scrolling scene.", 0.0, 1.0, 0.01, 1.0)}),
        EffectDef("wave_ripple", "Wave / Ripple", "Warp the game image outward in animated circular waves.", "Movement & Distortion",
                  {Target::PostFx},
                  {FloatParam("amount", "Warp Amount", "How far the image bends.", 0.0, 0.06, 0.001, 0.012),
                   FloatParam("frequency", "Wave Count", "How many ripples fit across the image.", 1.0, 32.0, 0.1, 8.0),
                   FloatParam("speed", "Speed", "How quickly the ripples travel.", -5.0, 5.0, 0.01, 0.8)}),
        EffectDef("paint_strokes", "Paint Strokes", "Rebuild the screen from jittered brush strokes that follow local image gradients instead of blocky pixel cells.", "Stylized Screen",
                  {Target::PostFx},
                  {FloatParam("strength", "Strength", "How much the painted reconstruction replaces the original scene.", 0.0, 1.0, 0.01, 0.88),
                   FloatParam("scale", "Brush Density", "Higher values create more, smaller brush strokes.", 8.0, 96.0, 1.0, 34.0),
                   FloatParam("smear", "Paint Smear", "How far color is pulled along each brush direction.", 0.0, 2.0, 0.01, 0.85),
                   FloatParam("bend", "Stroke Bend", "Curve the brush shape instead of keeping every stroke straight.", -1.0, 1.0, 0.01, 0.22),
                   FloatParam("detail", "Edge Detail", "Use a wider local gradient to orient strokes around larger forms.", 0.0, 1.0, 0.01, 0.45)}),
        EffectDef("red_paint_splatter", "Red Paint Splatter", "Overlay procedural red paint splashes and drips across the screen.", "Stylized Screen",
                  {Target::PostFx},
                  {ColorParam("color", "Paint Color", "Color of the splatter overlay.", "#9E1424"),
                   FloatParam("strength", "Strength", "How visible the splatter becomes.", 0.0, 1.0, 0.01, 0.55),
                   FloatParam("scale", "Splash Count", "Higher values create more, smaller splatters.", 2.0, 18.0, 0.1, 6.0)}),
        EffectDef("water_distortion", "Water Distortion", "Refract the screen like a watery surface or wet camera lens.", "Water & Weather",
                  {Target::PostFx},
                  {FloatParam("amount", "Distortion", "How far the refraction bends the image.", 0.0, 3.0, 0.01, 0.75),
                   FloatParam("scale", "Wave Scale", "How many waves fit across the screen.", 0.5, 20.0, 0.05, 6.0),
                   FloatParam("speed", "Speed", "How quickly the water motion animates.", -4.0, 4.0, 0.01, 0.85),
                   FloatParam("strength", "Strength", "Blend amount between the original scene and the distorted version.", 0.0, 1.0, 0.01, 1.0)}),

        EffectDef("sky_sun", "Atmospheric Sun / Time", "Move the sun through a full day/night cycle. Its direction drives the sky color, twilight, horizon haze and a soft Rayleigh/Mie-inspired sun instead of a blown-out flat disc.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Sun Color", "Base daylight color of the sun. Sunrise and sunset warm it automatically.", "#FFF1D2"),
                   FloatParam("time_of_day", "Time of Day", "Move the sun through the day. 6 = sunrise, 12 = noon, 18 = sunset, 0/24 = midnight.", 0.0, 24.0, 0.05, 14.0),
                   FloatParam("azimuth", "Sun Direction", "Rotate the sun path around the horizon. 0 and 1 meet seamlessly.", 0.0, 1.0, 0.01, 0.12),
                   FloatParam("height", "Sun Arc Height", "Maximum elevation of the sun at midday.", 0.20, 0.98, 0.01, 0.86),
                   FloatParam("size", "Disc Size", "Angular radius of the sun. Realistic values stay small.", 0.002, 0.030, 0.0005, 0.006),
                   FloatParam("softness", "Disc Softness", "Width of the sun-disc edge transition.", 0.0005, 0.012, 0.0005, 0.002),
                   FloatParam("brightness", "Sun Brightness", "Controlled HDR intensity of the sun disc.", 0.0, 6.0, 0.05, 2.4),
                   FloatParam("glow", "Mie Glow", "Width and strength of forward-scattered light around the sun.", 0.0, 1.0, 0.01, 0.42),
                   FloatParam("atmosphere", "Atmosphere", "How strongly the sun position recolors the entire sky.", 0.0, 1.0, 0.01, 0.92),
                   FloatParam("haze", "Horizon Haze", "Warm atmospheric haze near the horizon and around the sun.", 0.0, 1.0, 0.01, 0.46)}),
        EffectDef("sky_moon", "Moon & Halo", "Place an independent moon in the sky, including crescent phase and a soft halo.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Moon Color", "Color of the moon and halo.", "#DCE8FF"),
                   FloatParam("azimuth", "Horizontal Position", "Move the moon around the horizon. 0 and 1 meet seamlessly.", 0.0, 1.0, 0.01, 0.62),
                   FloatParam("height", "Height", "Vertical position of the moon.", -0.85, 0.95, 0.01, 0.48),
                   FloatParam("size", "Disc Size", "Angular radius of the moon disc.", 0.008, 0.20, 0.002, 0.055),
                   FloatParam("phase", "Moon Phase", "0 and 1 create opposite crescents; 0.5 is close to full.", 0.0, 1.0, 0.01, 0.50),
                   FloatParam("brightness", "Brightness", "HDR brightness of the moon.", 0.0, 6.0, 0.05, 1.35),
                   FloatParam("halo", "Halo", "Strength and width of the moon glow.", 0.0, 1.0, 0.01, 0.32)}),
        EffectDef("sky_haze", "Horizon Haze", "Add atmospheric haze around the horizon to blend mountains, clouds and distant sky layers together.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Haze Color", "Atmospheric color along the horizon.", "#A6B9CC"),
                   FloatParam("strength", "Strength", "How strongly the haze colors the horizon.", 0.0, 1.0, 0.01, 0.30),
                   FloatParam("width", "Vertical Width", "Higher values keep the haze closer to the horizon.", 0.5, 12.0, 0.1, 4.0)}),
        EffectDef("sky_stars", "Procedural Stars", "Scatter small direction-space stars across the sky without a texture map.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Star Color", "Base color of the star field.", "#DDEBFF"),
                   FloatParam("density", "Density", "Higher values create a denser star grid.", 40.0, 420.0, 1.0, 180.0),
                   FloatParam("size", "Star Size", "Radius of each star inside its procedural cell.", 0.03, 0.30, 0.005, 0.10),
                   FloatParam("brightness", "Brightness", "HDR brightness of the stars.", 0.0, 6.0, 0.05, 1.15),
                   FloatParam("twinkle", "Twinkle", "Amount of animated brightness variation.", 0.0, 1.0, 0.01, 0.25)}),
        EffectDef("sky_clouds", "Cloud Layers", "Add lightweight seamless procedural clouds with controllable altitude, brightness and wind direction.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Cloud Color", "Base color of the clouds.", "#DDE6EF"),
                   FloatParam("brightness", "Brightness", "Brightness multiplier for the cloud layer.", 0.10, 4.0, 0.05, 1.0),
                   FloatParam("opacity", "Opacity", "How strongly the clouds cover the sky behind them.", 0.0, 1.0, 0.01, 0.72),
                   FloatParam("height", "Cloud Height", "Raise or lower the visible cloud layer relative to the horizon.", -0.35, 0.75, 0.01, 0.06),
                   FloatParam("scale", "Cloud Scale", "Size and frequency of the cloud formations.", 0.8, 12.0, 0.05, 3.4),
                   FloatParam("coverage", "Coverage", "Higher values leave more open sky between cloud masses.", 0.15, 0.85, 0.01, 0.52),
                   FloatParam("softness", "Softness", "Feathering around cloud edges.", 0.02, 0.35, 0.01, 0.14),
                   FloatParam("direction", "Wind Direction", "Direction the cloud field travels, in degrees around the horizon.", 0.0, 360.0, 1.0, 25.0),
                   FloatParam("speed", "Wind Speed", "How quickly the procedural cloud field drifts. Negative values reverse it.", -3.0, 3.0, 0.01, 0.22)}),
        EffectDef("sky_realistic_clouds", "Volumetric Clouds", "Build a soft perspective cloud deck with broad formations, detail erosion, directional wind and sunlight without visible slice bands.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("shadow_color", "Shadow Color", "Color inside the darker cloud cavities.", "#54606D"),
                   ColorParam("light_color", "Light Color", "Color on the brighter parts of the cloud volume.", "#F2F4F6"),
                   FloatParam("brightness", "Brightness", "Cloud brightness before atmospheric blending.", 0.15, 2.0, 0.01, 0.92),
                   FloatParam("opacity", "Density", "Overall cloud-volume density.", 0.0, 1.0, 0.01, 0.62),
                   FloatParam("height", "Cloud Height", "Raise or lower the volumetric layer relative to the horizon.", -0.35, 0.75, 0.01, 0.16),
                   FloatParam("scale", "Formation Scale", "Scale of the cloud formations.", 0.5, 5.0, 0.05, 1.10),
                   FloatParam("coverage", "Coverage", "Higher values fill more of the sky with cloud.", 0.0, 1.0, 0.01, 0.48),
                   FloatParam("direction", "Wind Direction", "Direction the cloud volume moves, in degrees around the horizon.", 0.0, 360.0, 1.0, 35.0),
                   FloatParam("speed", "Wind Speed", "How quickly the cloud volume evolves and drifts. Negative values reverse it.", -2.0, 2.0, 0.01, 0.10)}),
        EffectDef("sky_water", "Still Water Reflection", "Turn the lower hemisphere into a still-water reflection of the complete procedural sky, with subtle animated ripples and tint.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("tint", "Water Tint", "Color mixed into the reflected lower hemisphere.", "#183344"),
                   FloatParam("reflection", "Reflection", "Brightness of the mirrored sky in the water.", 0.0, 1.5, 0.01, 0.72),
                   FloatParam("ripple", "Ripple Amount", "Small angular distortion applied to the reflected sky.", 0.0, 1.0, 0.01, 0.10),
                   FloatParam("scale", "Ripple Scale", "Size/frequency of the water ripples.", 1.0, 48.0, 0.5, 13.0),
                   FloatParam("speed", "Ripple Speed", "How quickly the still-water surface moves.", -2.0, 2.0, 0.01, 0.18),
                   FloatParam("horizon", "Horizon Blend", "Softness of the transition where sky meets water.", 0.001, 0.12, 0.002, 0.025)}),

        EffectDef("sky_mountains", "Mountain Range", "Generate three atmospheric mountain layers with broad terrain masses and restrained ridge detail around the horizon.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("near_color", "Near Mountains", "Color of the closest mountain range.", "#141922"),
                   ColorParam("far_color", "Distant Mountains", "Color of distant mountains after atmospheric perspective.", "#52637A"),
                   FloatParam("height", "Horizon Height", "Average height of the mountain range.", -0.25, 0.45, 0.01, 0.02),
                   FloatParam("roughness", "Peak Height", "Controls mountain relief without turning the skyline into spikes.", 0.02, 0.45, 0.01, 0.20),
                   FloatParam("scale", "Mountain Scale", "Controls the width and number of major terrain forms.", 1.0, 14.0, 0.1, 5.4),
                   FloatParam("softness", "Atmospheric Softness", "Softens the distant skyline and reduces aliasing.", 0.002, 0.06, 0.001, 0.014)}),
        EffectDef("sky_aurora", "Aurora Borealis", "Create layered moving aurora curtains with procedural triangular-domain warping instead of flat sine bands.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color_a", "Primary Color", "Main aurora color.", "#39F2A0"),
                   ColorParam("color_b", "Secondary Color", "Secondary color mixed through the moving ribbons.", "#6688FF"),
                   FloatParam("intensity", "Brightness", "HDR brightness of the aurora curtains.", 0.0, 5.0, 0.01, 1.15),
                   FloatParam("scale", "Ribbon Detail", "Frequency and complexity of the aurora folds.", 0.5, 12.0, 0.05, 4.2),
                   FloatParam("speed", "Movement Speed", "How quickly the ribbons flow and deform.", -3.0, 3.0, 0.01, 0.45),
                   FloatParam("azimuth", "Horizontal Position", "Rotate the aurora around the sky.", 0.0, 1.0, 0.01, 0.18)}),
        EffectDef("sky_nebula", "Fractal Nebula", "Add drifting folded-fractal color structures directly in BO3 sky direction space.", "Space & Cosmic",
                  {Target::Sky},
                  {ColorParam("color_a", "Primary Color", "First nebula color.", "#365CFF"),
                   ColorParam("color_b", "Secondary Color", "Second color inside the fractal structure.", "#A03BCE"),
                   FloatParam("intensity", "Brightness", "Brightness of the nebula field.", 0.0, 4.0, 0.01, 0.85),
                   FloatParam("scale", "Scale", "Size and complexity of the nebula regions.", 0.35, 3.0, 0.01, 0.82),
                   FloatParam("drift", "Drift", "How far the fractal domain slowly moves over time.", 0.0, 1.0, 0.01, 0.18)}),

        EffectDef("edge_glow", "Edge Glow", "Add a camera-facing rim glow around the edges of a model.", "Material & Glow",
                  {Target::Material},
                  {ColorParam("color", "Glow Color", "Color of the rim light.", "#6FE8FF"),
                   FloatParam("strength", "Strength", "How bright the edge glow becomes.", 0.0, 5.0, 0.01, 1.25),
                   FloatParam("power", "Edge Width", "Lower values make a wider rim; higher values tighten it.", 0.5, 8.0, 0.05, 2.5)}),
        EffectDef("emission", "Emission", "Add BO3-friendly HDR color so a material can look self-lit and energetic.", "Material & Glow",
                  {Target::Material},
                  {ColorParam("color", "Emission Color", "Color emitted by the surface.", "#45DFFF"),
                   FloatParam("strength", "Brightness", "HDR emission strength. Values above 1 can glow strongly in BO3.", 0.0, 8.0, 0.05, 1.4)}),
        EffectDef("dissolve", "Dissolve", "Cut away parts of a material with a procedural pattern and a bright edge.", "Material & Glow",
                  {Target::Material},
                  {FloatParam("amount", "Dissolve Amount", "0 keeps the surface; higher values remove more of it.", 0.0, 0.95, 0.01, 0.28),
                   FloatParam("scale", "Pattern Scale", "Size of the dissolve pattern.", 4.0, 400.0, 1.0, 90.0),
                   FloatParam("edge_width", "Edge Width", "Width of the bright transition around dissolving areas.", 0.01, 0.25, 0.005, 0.08),
                   ColorParam("edge_color", "Edge Color", "Color along the dissolving edge.", "#FF8A3D")}),

        EffectDef("gradient", "Color Gradient", "Blend a top and bottom color through the screen or sky while keeping its detail.", "Patterns",
                  {Target::PostFx, Target::Sky},
                  {ColorParam("bottom_color", "Bottom Color", "Color toward the bottom.", "#5740A8"),
                   ColorParam("top_color", "Top Color", "Color toward the top.", "#63D8D0"),
                   FloatParam("strength", "Strength", "How strongly the gradient colors the result.", 0.0, 1.0, 0.01, 0.40)}),
        EffectDef("grid_rings", "Grid / Rings", "Overlay a procedural grid or rings without requiring an image texture.", "Patterns",
                  {Target::PostFx, Target::Material},
                  {ColorParam("color", "Pattern Color", "Color of the lines.", "#6FE8FF"),
                   FloatParam("strength", "Strength", "Brightness of the pattern.", 0.0, 3.0, 0.01, 0.45),
                   FloatParam("scale", "Scale", "How many pattern cells or rings are visible.", 2.0, 80.0, 0.1, 12.0),
                   FloatParam("rings", "Grid / Rings Mix", "0 is a grid; 1 is rings; values between blend both.", 0.0, 1.0, 0.01, 0.0)})
    };
    return definitions;
}

const EffectDefinition* effectDefinition(const QString& id)
{
    for(const EffectDefinition& definition : effectDefinitions())
        if(definition.id.compare(id, Qt::CaseInsensitive) == 0) return &definition;
    return nullptr;
}

bool supportsTarget(const EffectDefinition& definition, Target target)
{
    return definition.targets.contains(target);
}

Effect makeDefaultEffect(const QString& typeId)
{
    Effect effect;
    effect.instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const EffectDefinition* definition = effectDefinition(typeId);
    if(!definition) return effect;
    effect.typeId = definition->id;
    for(const ParameterDefinition& parameter : definition->parameters)
    {
        if(parameter.kind == ParameterKind::Color)
            effect.parameters[parameter.key] = parameter.defaultColor.name(QColor::HexRgb);
        else
            effect.parameters[parameter.key] = parameter.defaultValue;
    }
    return effect;
}

Project makeDefaultProject(Target target)
{
    Project project;
    project.target = target;
    project.name = target == Target::Material ? "New Material" :
                   target == Target::Sky ? "New Sky" : "New Screen Effect";
    if(target == Target::Material)
        project.settings["baseColor"] = "#2F78D0";
    else if(target == Target::Sky)
    {
        project.settings["zenithColor"] = "#102E68";
        project.settings["horizonColor"] = "#E17658";
        project.settings["groundColor"] = "#060B18";
    }
    return project;
}

QVector<QPair<QString, QString>> presetsForTarget(Target target)
{
    if(target == Target::PostFx)
        return {{"blank", "Blank / Original Scene"}, {"cinematic", "Cinematic"}, {"retro_crt", "Retro CRT"}};
    if(target == Target::Material)
        return {{"blank", "Blank Surface"}, {"neon_surface", "Neon Surface"}, {"hologram", "Hologram"}};
    return {{"blank", "Blank Sky"}, {"sunset", "Sunset"}, {"lake_sunset", "Still Water Sunset"},
                {"mountain_dawn", "Mountain Dawn"}, {"cloudy_day", "Cloudy Day"}, {"starry_night", "Starry Night"},
                {"aurora_night", "Aurora Night"}, {"dream_sky", "Dream Sky"}, {"space_nebula", "Space Nebula"}};
}

Project makePreset(const QString& presetId, Target target)
{
    Project project = makeDefaultProject(target);
    const QString id = presetId.trimmed().toLower();
    if(id == "blank") return project;

    auto add = [&](const QString& typeId, std::initializer_list<QPair<QString, QVariant>> values)
    {
        Effect effect = makeDefaultEffect(typeId);
        for(const auto& value : values) effect.parameters[value.first] = QJsonValue::fromVariant(value.second);
        project.effects.push_back(effect);
    };

    if(target == Target::PostFx && id == "cinematic")
    {
        project.name = "Cinematic Screen";
        add("contrast", {{"amount", 1.18}});
        add("saturation", {{"amount", 0.92}});
        add("vignette", {{"strength", 0.34}, {"size", 0.72}, {"softness", 0.38}});
    }
    else if(target == Target::PostFx && id == "retro_crt")
    {
        project.name = "Retro CRT";
        add("saturation", {{"amount", 0.84}});
        add("scanlines", {{"amount", 0.18}, {"density", 190.0}, {"speed", 0.20}});
        add("film_grain", {{"amount", 0.22}, {"grain_size", 1.30}, {"speed", 1.0}});
        add("chromatic_aberration", {{"amount", 0.0025}, {"strength", 0.38}});
        add("vignette", {{"strength", 0.28}, {"size", 0.68}, {"softness", 0.45}});
    }
    else if(target == Target::Material && id == "neon_surface")
    {
        project.name = "Neon Surface";
        project.settings["baseColor"] = "#1949A8";
        add("tint", {{"color", "#52E8FF"}, {"amount", 0.55}});
        add("emission", {{"color", "#45DFFF"}, {"strength", 1.35}});
        add("edge_glow", {{"color", "#8FF5FF"}, {"strength", 1.10}, {"power", 2.2}});
        add("pulse", {{"amount", 0.16}, {"speed", 0.65}});
        add("saturation", {{"amount", 1.35}});
    }
    else if(target == Target::Material && id == "hologram")
    {
        project.name = "Hologram";
        project.settings["baseColor"] = "#1E6B84";
        add("tint", {{"color", "#7CF5FF"}, {"amount", 0.66}});
        add("emission", {{"color", "#4DE7FF"}, {"strength", 0.90}});
        add("edge_glow", {{"color", "#9AFAFF"}, {"strength", 1.35}, {"power", 2.0}});
        add("scanlines", {{"amount", 0.16}, {"density", 92.0}, {"speed", 0.55}});
        add("noise", {{"amount", 0.045}, {"scale", 260.0}, {"speed", 0.75}});
        add("flicker", {{"amount", 0.10}, {"speed", 1.4}});
        add("pulse", {{"amount", 0.08}, {"speed", 1.10}});
    }
    else if(target == Target::Sky && id == "sunset")
    {
        project.name = "Sunset Sky";
        project.settings["zenithColor"] = "#17336F";
        project.settings["horizonColor"] = "#F47A52";
        project.settings["groundColor"] = "#120A16";
        add("sky_sun", {{"color", "#FFF1D2"}, {"time_of_day", 17.35}, {"azimuth", 0.14}, {"height", 0.86}, {"size", 0.006}, {"softness", 0.002}, {"brightness", 2.2}, {"glow", 0.44}, {"atmosphere", 0.94}, {"haze", 0.46}});
        add("sky_clouds", {{"color", "#E8C2B0"}, {"brightness", 1.05}, {"opacity", 0.46}, {"height", 0.10}, {"scale", 2.8}, {"coverage", 0.54}, {"direction", 18.0}, {"speed", 0.12}});
        add("saturation", {{"amount", 1.18}});
        add("contrast", {{"amount", 1.06}});
    }
    else if(target == Target::Sky && id == "lake_sunset")
    {
        project.name = "Still Water Sunset";
        project.settings["zenithColor"] = "#17336F";
        project.settings["horizonColor"] = "#F47A52";
        project.settings["groundColor"] = "#0A1722";
        add("sky_sun", {{"color", "#FFF0D0"}, {"time_of_day", 17.25}, {"azimuth", 0.12}, {"height", 0.88}, {"size", 0.006}, {"softness", 0.002}, {"brightness", 2.35}, {"glow", 0.46}, {"atmosphere", 0.96}, {"haze", 0.50}});
        add("sky_realistic_clouds", {{"shadow_color", "#775B60"}, {"light_color", "#F2D2B5"}, {"brightness", 0.92}, {"opacity", 0.64}, {"height", 0.12}, {"scale", 1.35}, {"coverage", 0.58}, {"direction", 22.0}, {"speed", 0.10}});
        add("sky_water", {{"tint", "#16384A"}, {"reflection", 0.78}, {"ripple", 0.085}, {"scale", 14.0}, {"speed", 0.16}, {"horizon", 0.022}});
    }
    else if(target == Target::Sky && id == "mountain_dawn")
    {
        project.name = "Mountain Dawn";
        project.settings["zenithColor"] = "#345A83";
        project.settings["horizonColor"] = "#E9A06E";
        project.settings["groundColor"] = "#10131C";
        add("sky_sun", {{"color", "#FFF0D0"}, {"time_of_day", 6.8}, {"azimuth", 0.07}, {"height", 0.84}, {"size", 0.0055}, {"softness", 0.002}, {"brightness", 2.15}, {"glow", 0.40}, {"atmosphere", 0.94}, {"haze", 0.48}});
        add("sky_haze", {{"color", "#D8AF92"}, {"strength", 0.32}, {"width", 4.8}});
        add("sky_mountains", {{"near_color", "#171B22"}, {"far_color", "#5B6A7B"}, {"height", 0.025}, {"roughness", 0.19}, {"scale", 5.3}, {"softness", 0.014}});
        add("sky_clouds", {{"color", "#D8DFE6"}, {"opacity", 0.35}, {"coverage", 0.60}, {"speed", 0.10}});
    }
    else if(target == Target::Sky && id == "cloudy_day")
    {
        project.name = "Cloudy Day";
        project.settings["zenithColor"] = "#536C83";
        project.settings["horizonColor"] = "#A9BAC7";
        project.settings["groundColor"] = "#27323D";
        add("sky_sun", {{"color", "#F4F6F8"}, {"time_of_day", 13.2}, {"azimuth", 0.30}, {"height", 0.88}, {"size", 0.0048}, {"softness", 0.0015}, {"brightness", 1.65}, {"glow", 0.24}, {"atmosphere", 0.78}, {"haze", 0.24}});
        add("sky_realistic_clouds", {{"shadow_color", "#596774"}, {"light_color", "#EDF1F4"}, {"brightness", 0.92}, {"opacity", 0.64}, {"height", 0.20}, {"scale", 1.08}, {"coverage", 0.50}, {"direction", 35.0}, {"speed", 0.14}});
    }
    else if(target == Target::Sky && id == "starry_night")
    {
        project.name = "Starry Night";
        project.settings["zenithColor"] = "#07132E";
        project.settings["horizonColor"] = "#182847";
        project.settings["groundColor"] = "#03050B";
        add("sky_stars", {{"density", 210.0}, {"size", 0.085}, {"brightness", 1.35}, {"twinkle", 0.32}});
        add("sky_moon", {{"color", "#DCE8FF"}, {"azimuth", 0.68}, {"height", 0.48}, {"size", 0.050}, {"phase", 0.56}, {"brightness", 1.45}, {"halo", 0.38}});
        add("sky_mountains", {{"near_color", "#07090D"}, {"far_color", "#172233"}, {"height", -0.02}, {"roughness", 0.17}, {"scale", 5.8}, {"softness", 0.012}});
    }
    else if(target == Target::Sky && id == "aurora_night")
    {
        project.name = "Aurora Night";
        project.settings["zenithColor"] = "#061428";
        project.settings["horizonColor"] = "#102238";
        project.settings["groundColor"] = "#02050A";
        add("sky_stars", {{"density", 170.0}, {"brightness", 0.80}, {"twinkle", 0.18}});
        add("sky_aurora", {{"color_a", "#39EFA0"}, {"color_b", "#6278FF"}, {"intensity", 1.25}, {"scale", 4.5}, {"speed", 0.42}, {"azimuth", 0.18}});
        add("sky_mountains", {{"near_color", "#06080B"}, {"far_color", "#14202B"}, {"height", -0.04}, {"roughness", 0.15}, {"scale", 5.2}, {"softness", 0.012}});
    }
    else if(target == Target::Sky && id == "dream_sky")
    {
        project.name = "Dream Sky";
        project.settings["zenithColor"] = "#24124F";
        project.settings["horizonColor"] = "#8D4F9B";
        project.settings["groundColor"] = "#070814";
        add("sky_stars", {{"color", "#DCE5FF"}, {"density", 155.0}, {"brightness", 0.70}, {"twinkle", 0.45}});
        add("sky_nebula", {{"color_a", "#3D62FF"}, {"color_b", "#B443CA"}, {"intensity", 0.55}, {"scale", 0.86}, {"drift", 0.20}});
        add("tint", {{"color", "#8FA7FF"}, {"amount", 0.12}});
    }
    else if(target == Target::Sky && id == "space_nebula")
    {
        project.name = "Space Nebula";
        project.settings["zenithColor"] = "#020817";
        project.settings["horizonColor"] = "#071229";
        project.settings["groundColor"] = "#01030A";
        add("sky_nebula", {{"color_a", "#245DFF"}, {"color_b", "#A333D0"}, {"intensity", 1.10}, {"scale", 0.76}, {"drift", 0.16}});
        add("sky_stars", {{"density", 240.0}, {"size", 0.08}, {"brightness", 1.25}, {"twinkle", 0.18}});
    }
    return project;
}

QJsonObject projectToJson(const Project& project)
{
    QJsonObject root;
    root["format"] = "BO3 Shader Studio Beginner Project";
    root["version"] = project.version;
    root["name"] = project.name;
    root["target"] = targetId(project.target);
    root["settings"] = project.settings;
    QJsonArray effects;
    for(const Effect& effect : project.effects)
    {
        QJsonObject object;
        object["instanceId"] = effect.instanceId;
        object["type"] = effect.typeId;
        object["enabled"] = effect.enabled;
        object["parameters"] = effect.parameters;
        effects.append(object);
    }
    root["effects"] = effects;
    return root;
}

bool projectFromJson(const QJsonObject& object, Project& project, QString& error)
{
    error.clear();
    if(object.value("format").toString() != "BO3 Shader Studio Beginner Project")
    {
        error = "This file is not a BO3 Shader Studio Beginner Project.";
        return false;
    }
    const int version = object.value("version").toInt(1);
    if(version != 1)
    {
        error = QString("Beginner project version %1 is not supported by this build.").arg(version);
        return false;
    }
    Target target;
    if(!targetFromId(object.value("target").toString(), target))
    {
        error = "The project has an unknown shader target.";
        return false;
    }

    Project loaded = makeDefaultProject(target);
    loaded.version = version;
    loaded.name = object.value("name").toString(loaded.name).trimmed();
    if(loaded.name.isEmpty()) loaded.name = "Untitled Shader";
    if(object.value("settings").isObject())
    {
        const QJsonObject settings = object.value("settings").toObject();
        for(auto it = settings.begin(); it != settings.end(); ++it)
            loaded.settings[it.key()] = it.value();
    }

    const QJsonArray effects = object.value("effects").toArray();
    for(const QJsonValue& value : effects)
    {
        if(!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const QString typeId = item.value("type").toString();
        const EffectDefinition* definition = effectDefinition(typeId);
        if(!definition || !supportsTarget(*definition, target)) continue;
        Effect effect = makeDefaultEffect(typeId);
        const QString instanceId = item.value("instanceId").toString().trimmed();
        if(!instanceId.isEmpty()) effect.instanceId = instanceId;
        effect.enabled = item.value("enabled").toBool(true);
        if(item.value("parameters").isObject())
        {
            const QJsonObject parameters = item.value("parameters").toObject();
            for(const ParameterDefinition& parameter : definition->parameters)
            {
                if(!parameters.contains(parameter.key)) continue;
                if(parameter.kind == ParameterKind::Color)
                {
                    const QColor candidate(parameters.value(parameter.key).toString());
                    if(candidate.isValid()) effect.parameters[parameter.key] = candidate.name(QColor::HexRgb);
                }
                else
                {
                    const double candidate = parameters.value(parameter.key).toDouble(parameter.defaultValue);
                    effect.parameters[parameter.key] = std::clamp(candidate, parameter.minimum, parameter.maximum);
                }
            }
        }
        loaded.effects.push_back(effect);
    }

    project = loaded;
    return true;
}

QString generateHlsl(const Project& project, QStringList* notes)
{
    if(notes)
    {
        notes->clear();
        notes->append(QString("Beginner Builder generated a BO3 %1 shader from validated internal modules.").arg(targetName(project.target)));
        for(const Effect& effect : project.effects)
        {
            const EffectDefinition* definition = effectDefinition(effect.typeId);
            if(effect.enabled && definition && supportsTarget(*definition, project.target))
                notes->append(QString("Effect: %1").arg(definition->name));
        }
    }
    switch(project.target)
    {
        case Target::Material: return generateMaterial(project);
        case Target::Sky: return generateSky(project);
        default: return generatePostFx(project);
    }
}

QString projectSummary(const Project& project)
{
    QStringList parts;
    if(project.target == Target::PostFx) parts << "Game scene";
    else if(project.target == Target::Material) parts << "Base color";
    else parts << "Sky gradient";
    for(const Effect& effect : project.effects)
    {
        if(!effect.enabled) continue;
        const EffectDefinition* definition = effectDefinition(effect.typeId);
        if(definition && supportsTarget(*definition, project.target)) parts << definition->name;
    }
    return parts.join("  →  ");
}

QString compatibilitySummary(const Project& project)
{
    int enabled = 0;
    for(const Effect& effect : project.effects)
    {
        const EffectDefinition* definition = effectDefinition(effect.typeId);
        if(effect.enabled && definition && supportsTarget(*definition, project.target)) ++enabled;
    }
    return QString("BO3-safe modules only • %1 • %2 effect%3")
        .arg(targetName(project.target))
        .arg(enabled)
        .arg(enabled == 1 ? "" : "s");
}

} // namespace beginner
