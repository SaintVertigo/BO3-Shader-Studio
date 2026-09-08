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
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1\n"
                           "    float %2_grain = BO3BeginnerHash21(floor(uv * %3) + floor(t * %4 * 59.0));\n"
                           "    float %2_lumaMask = 0.45 + 0.55 * saturate(1.0 - dot(saturate(color), float3(0.2126, 0.7152, 0.0722)));\n"
                           "    color += (%2_grain - 0.5) * %5 * %2_lumaMask;\n")
                .arg(definition->name, tag, scale, speed, amount);
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

QString optionalHelpers(const Project& project)
{
    QString out;
    const bool needsHash21 =
        (project.target != Target::Material && projectUsesEffect(project, "noise")) ||
        projectUsesEffect(project, "film_grain");
    const bool needsHash31 =
        (project.target == Target::Material && projectUsesEffect(project, "noise")) ||
        projectUsesEffect(project, "dissolve");
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
    const QColor horizon = settingColor(project, "horizonColor", QColor("#E17658"));
    const QColor ground = settingColor(project, "groundColor", QColor("#060B18"));
    const QString helpers = optionalHelpers(project);
    const QString effects = commonEffectCode(project, true, false);
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
    float3 d = normalize(input.skyDirection.xyz);
    float t = gameTime.w;
    float horizon = saturate(1.0 - abs(d.z));
    float up = saturate(d.z * 0.5 + 0.5);
    float beginnerVertical = up;
    float3 color = lerp(%3, %4, smoothstep(0.0, 0.62, up));
    color = lerp(color, %5, pow(horizon, 5.0) * 0.72);
%6
    return float4(saturate(color), 1.0);
}
)HLSL").arg(effectStackMarker(project), helpers, colorLiteral(ground), colorLiteral(zenith), colorLiteral(horizon), effects);
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
        EffectDef("film_grain", "Film Grain", "Add fine moving grain that is strongest in darker parts of the game image.", "Atmosphere",
                  {Target::PostFx},
                  {FloatParam("amount", "Strength", "How visible the grain is.", 0.0, 0.30, 0.005, 0.045),
                   FloatParam("scale", "Grain Size", "Higher values make the grain finer.", 120.0, 1800.0, 1.0, 720.0),
                   FloatParam("speed", "Speed", "How quickly the grain changes.", 0.0, 4.0, 0.01, 1.0)}),

        EffectDef("scanlines", "Scanlines", "Add animated horizontal lines for CRT, hologram, visor and monitor looks.", "Retro & Display",
                  {Target::PostFx, Target::Material},
                  {FloatParam("amount", "Strength", "How dark the scanlines become.", 0.0, 0.75, 0.01, 0.12),
                   FloatParam("density", "Density", "Number of line cycles across the surface.", 8.0, 800.0, 1.0, 160.0),
                   FloatParam("speed", "Speed", "How quickly the line pattern moves.", -4.0, 4.0, 0.01, 0.25)}),
        EffectDef("chromatic_aberration", "Chromatic Aberration", "Separate the red and blue channels near edges for a lens or glitch look.", "Retro & Display",
                  {Target::PostFx},
                  {FloatParam("amount", "Channel Offset", "How far the red and blue channels separate.", 0.0, 0.025, 0.0005, 0.004),
                   FloatParam("strength", "Strength", "How much of the channel separation is added.", 0.0, 1.0, 0.01, 0.65)}),

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
                   FloatParam("rings", "Grid ↔ Rings", "0 is a grid; 1 is rings; values between blend both.", 0.0, 1.0, 0.01, 0.0)})
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
    return {{"blank", "Blank Sky"}, {"sunset", "Sunset"}, {"dream_sky", "Dream Sky"}};
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
        add("film_grain", {{"amount", 0.035}, {"scale", 760.0}, {"speed", 1.0}});
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
        project.settings["zenithColor"] = "#193B85";
        project.settings["horizonColor"] = "#FF8059";
        project.settings["groundColor"] = "#120B18";
        add("saturation", {{"amount", 1.18}});
        add("contrast", {{"amount", 1.08}});
    }
    else if(target == Target::Sky && id == "dream_sky")
    {
        project.name = "Dream Sky";
        project.settings["zenithColor"] = "#24124F";
        project.settings["horizonColor"] = "#B85BBE";
        project.settings["groundColor"] = "#070814";
        add("tint", {{"color", "#8FA7FF"}, {"amount", 0.22}});
        add("saturation", {{"amount", 1.32}});
        add("pulse", {{"amount", 0.06}, {"speed", 0.22}});
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
