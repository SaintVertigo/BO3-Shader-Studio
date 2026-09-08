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
            const QString grainSize = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            const QString shadowBoost = floatLiteral(parameterFloat(effect, *definition, "shadow_boost"));
            const QString colorGrain = floatLiteral(parameterFloat(effect, *definition, "color_grain"));
            out += QString("    // %1 - true pixel-space film grain, not scanline-sized UV bands\n"
                           "    float %2_frame = floor(t * %4 * 48.0);\n"
                           "    float2 %2_grainCell = floor(beginnerPixel / max(%3, 0.5));\n"
                           "    float %2_g0 = BO3BeginnerHash21(%2_grainCell + float2(%2_frame * 17.0, %2_frame * 31.0)) - 0.5;\n"
                           "    float %2_g1 = BO3BeginnerHash21(%2_grainCell.yx + float2(%2_frame * 47.0 + 13.0, %2_frame * 11.0 + 7.0)) - 0.5;\n"
                           "    float %2_g2 = BO3BeginnerHash21(%2_grainCell + float2(%2_frame * 23.0 + 37.0, %2_frame * 53.0 + 19.0)) - 0.5;\n"
                           "    float %2_luma = dot(saturate(color), float3(0.2126, 0.7152, 0.0722));\n"
                           "    float %2_lumaMask = lerp(1.0, 1.45 - 0.80 * %2_luma, %5);\n"
                           "    float3 %2_mono = %2_g0.xxx;\n"
                           "    float3 %2_rgb = float3(%2_g0, %2_g1, %2_g2);\n"
                           "    color += lerp(%2_mono, %2_rgb, %6) * (%7 * %2_lumaMask);\n")
                .arg(definition->name, tag, grainSize, speed, shadowBoost, colorGrain, amount);
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
            out += QString("    // %1 - lens-style radial channel separation that grows toward the edges\n"
                           "    float2 %2_chromaCenter = uv - 0.5;\n"
                           "    float2 %2_chromaOffset = %2_chromaCenter * %3;\n"
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
        else if(effect.typeId == "sky_sun" && project.target == Target::Sky)
        {
            const QColor sunColor = parameterColor(effect, *definition, "color");
            const double az = parameterFloat(effect, *definition, "azimuth") * 3.14159265358979323846 / 180.0;
            const double el = parameterFloat(effect, *definition, "elevation") * 3.14159265358979323846 / 180.0;
            const double radius = parameterFloat(effect, *definition, "size") * 3.14159265358979323846 / 180.0;
            const double softness = parameterFloat(effect, *definition, "softness") * 3.14159265358979323846 / 180.0;
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString glow = floatLiteral(parameterFloat(effect, *definition, "glow"));
            const QString dir = QString("float3(%1, %2, %3)")
                .arg(floatLiteral(std::cos(el) * std::cos(az)),
                     floatLiteral(std::cos(el) * std::sin(az)),
                     floatLiteral(std::sin(el)));
            out += QString("    // %1\n"
                           "    float3 %2_sunDir = normalize(%3);\n"
                           "    float %2_sunDot = dot(d, %2_sunDir);\n"
                           "    float %2_sunDisc = smoothstep(%4, %5, %2_sunDot);\n"
                           "    float %2_sunHalo = pow(saturate(%2_sunDot), 72.0);\n"
                           "    color += %6 * (%2_sunDisc * %7 + %2_sunHalo * %8);\n")
                .arg(definition->name, tag, dir,
                     floatLiteral(std::cos(radius + softness)),
                     floatLiteral(std::cos(radius)),
                     colorLiteral(sunColor), brightness, glow);
        }
        else if(effect.typeId == "sky_moon" && project.target == Target::Sky)
        {
            const QColor moonColor = parameterColor(effect, *definition, "color");
            const double az = parameterFloat(effect, *definition, "azimuth") * 3.14159265358979323846 / 180.0;
            const double el = parameterFloat(effect, *definition, "elevation") * 3.14159265358979323846 / 180.0;
            const double radius = parameterFloat(effect, *definition, "size") * 3.14159265358979323846 / 180.0;
            const double softness = parameterFloat(effect, *definition, "softness") * 3.14159265358979323846 / 180.0;
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString halo = floatLiteral(parameterFloat(effect, *definition, "halo"));
            const QString dir = QString("float3(%1, %2, %3)")
                .arg(floatLiteral(std::cos(el) * std::cos(az)),
                     floatLiteral(std::cos(el) * std::sin(az)),
                     floatLiteral(std::sin(el)));
            out += QString("    // %1\n"
                           "    float3 %2_moonDir = normalize(%3);\n"
                           "    float %2_moonDot = dot(d, %2_moonDir);\n"
                           "    float %2_moonDisc = smoothstep(%4, %5, %2_moonDot);\n"
                           "    float %2_moonHalo = pow(saturate(%2_moonDot), 110.0);\n"
                           "    color += %6 * (%2_moonDisc * %7 + %2_moonHalo * %8);\n")
                .arg(definition->name, tag, dir,
                     floatLiteral(std::cos(radius + softness)),
                     floatLiteral(std::cos(radius)),
                     colorLiteral(moonColor), brightness, halo);
        }
        else if(effect.typeId == "sky_clouds" && project.target == Target::Sky)
        {
            const QColor cloudColor = parameterColor(effect, *definition, "color");
            const QString coverage = floatLiteral(parameterFloat(effect, *definition, "coverage"));
            const QString opacity = floatLiteral(parameterFloat(effect, *definition, "opacity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString softness = floatLiteral(parameterFloat(effect, *definition, "softness"));
            const double windAngle = parameterFloat(effect, *definition, "wind_direction") * 3.14159265358979323846 / 180.0;
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1 - seamless direction-space procedural clouds\n"
                           "    float3 %2_wind = float3(%3, %4, 0.0);\n"
                           "    float3 %2_cloudP = d * (%5 * 3.5) + %2_wind * (t * %6);\n"
                           "    float %2_cloudNoise = BO3BeginnerFbm3(%2_cloudP);\n"
                           "    float %2_cloudThreshold = lerp(0.76, 0.30, %7);\n"
                           "    float %2_cloud = smoothstep(%2_cloudThreshold, %2_cloudThreshold + max(%8, 0.025), %2_cloudNoise);\n"
                           "    %2_cloud *= smoothstep(-0.08, 0.16, d.z);\n"
                           "    float3 %2_cloudLit = %9 * (0.72 + 0.28 * saturate(d.z * 0.5 + 0.5));\n"
                           "    color = lerp(color, %2_cloudLit, saturate(%2_cloud * %10));\n")
                .arg(definition->name, tag,
                     floatLiteral(std::cos(windAngle)), floatLiteral(std::sin(windAngle)),
                     scale, speed, coverage, softness, colorLiteral(cloudColor))
                .arg(opacity);
        }
        else if(effect.typeId == "sky_mountains" && project.target == Target::Sky)
        {
            const QColor nearColor = parameterColor(effect, *definition, "near_color");
            const QColor farColor = parameterColor(effect, *definition, "far_color");
            const QString height = floatLiteral(parameterFloat(effect, *definition, "height"));
            const QString roughness = floatLiteral(parameterFloat(effect, *definition, "roughness"));
            const QString depth = floatLiteral(parameterFloat(effect, *definition, "depth"));
            out += QString("    // %1 - seam-free procedural horizon silhouettes\n"
                           "    float2 %2_h = normalize(d.xy + float2(0.00001, 0.00002));\n"
                           "    float %2_ridgeA = 0.5 + 0.5 * sin(%2_h.x * 14.7 + %2_h.y * 21.3 + 0.6);\n"
                           "    float %2_ridgeB = 0.5 + 0.5 * sin(%2_h.x * 31.9 - %2_h.y * 24.1 + 2.4);\n"
                           "    float %2_ridgeC = 0.5 + 0.5 * sin(%2_h.x * 59.3 + %2_h.y * 43.7 + 1.2);\n"
                           "    float %2_ridge = (%2_ridgeA * 0.52 + %2_ridgeB * 0.31 + %2_ridgeC * 0.17);\n"
                           "    float %2_farTop = %3 * 0.62 + (%2_ridge * 0.085 * %4);\n"
                           "    float %2_nearTop = %3 + pow(saturate(%2_ridge), 1.35) * (0.15 * %4);\n"
                           "    float %2_farMask = 1.0 - smoothstep(%2_farTop, %2_farTop + 0.012, d.z);\n"
                           "    float %2_nearMask = 1.0 - smoothstep(%2_nearTop, %2_nearTop + 0.009, d.z);\n"
                           "    color = lerp(color, %5, saturate(%2_farMask * %6));\n"
                           "    color = lerp(color, %7, %2_nearMask);\n")
                .arg(definition->name, tag, height, roughness, colorLiteral(farColor), depth, colorLiteral(nearColor));
        }
        else if(effect.typeId == "sky_stars" && project.target == Target::Sky)
        {
            const QColor starColor = parameterColor(effect, *definition, "color");
            const QString density = floatLiteral(parameterFloat(effect, *definition, "density"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString brightness = floatLiteral(parameterFloat(effect, *definition, "brightness"));
            const QString twinkle = floatLiteral(parameterFloat(effect, *definition, "twinkle"));
            const QString twinkleSpeed = floatLiteral(parameterFloat(effect, *definition, "twinkle_speed"));
            out += QString("    // %1\n"
                           "    float3 %2_starCell = floor(d * %3);\n"
                           "    float %2_starSeed = BO3BeginnerHash31(%2_starCell);\n"
                           "    float %2_starThreshold = lerp(0.9993, 0.972, %4);\n"
                           "    float %2_star = smoothstep(%2_starThreshold, 1.0, %2_starSeed);\n"
                           "    %2_star = pow(saturate(%2_star), 6.0);\n"
                           "    float %2_twinkle = 0.72 + 0.28 * sin(t * %5 * 6.2831853 + BO3BeginnerHash31(%2_starCell + 17.0) * 19.0);\n"
                           "    %2_star *= lerp(1.0, %2_twinkle, %6) * smoothstep(-0.04, 0.18, d.z);\n"
                           "    color += %7 * (%2_star * %8);\n")
                .arg(definition->name, tag, scale, density, twinkleSpeed, twinkle, colorLiteral(starColor), brightness);
        }
        else if(effect.typeId == "sky_haze" && project.target == Target::Sky)
        {
            const QColor hazeColor = parameterColor(effect, *definition, "color");
            const QString intensity = floatLiteral(parameterFloat(effect, *definition, "intensity"));
            const QString width = floatLiteral(parameterFloat(effect, *definition, "width"));
            out += QString("    // %1\n"
                           "    float %2_haze = exp(-abs(d.z) / max(%3, 0.01));\n"
                           "    color = lerp(color, %4, saturate(%2_haze * %5));\n")
                .arg(definition->name, tag, width, colorLiteral(hazeColor), intensity);
        }
        else if(effect.typeId == "sky_aurora" && project.target == Target::Sky)
        {
            const QColor colorA = parameterColor(effect, *definition, "color_a");
            const QColor colorB = parameterColor(effect, *definition, "color_b");
            const QString intensity = floatLiteral(parameterFloat(effect, *definition, "intensity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString speed = floatLiteral(parameterFloat(effect, *definition, "speed"));
            out += QString("    // %1\n"
                           "    float2 %2_ah = normalize(d.xy + float2(0.00001, 0.00002));\n"
                           "    float %2_an = BO3BeginnerFbm3(float3(%2_ah * %3, d.z * 2.0 + t * %4));\n"
                           "    float %2_wave = 0.5 + 0.5 * sin((%2_ah.x * 8.0 + %2_ah.y * 11.0 + %2_an * 3.2 + t * %4) * 3.14159265);\n"
                           "    float %2_band = smoothstep(0.58, 0.94, %2_wave);\n"
                           "    %2_band *= smoothstep(0.02, 0.28, d.z) * (1.0 - smoothstep(0.88, 1.0, d.z));\n"
                           "    float3 %2_ac = lerp(%5, %6, saturate(d.z));\n"
                           "    color += %2_ac * (%2_band * %7);\n")
                .arg(definition->name, tag, scale, speed, colorLiteral(colorA), colorLiteral(colorB), intensity);
        }
        else if(effect.typeId == "sky_nebula" && project.target == Target::Sky)
        {
            const QColor colorA = parameterColor(effect, *definition, "color_a");
            const QColor colorB = parameterColor(effect, *definition, "color_b");
            const QString intensity = floatLiteral(parameterFloat(effect, *definition, "intensity"));
            const QString scale = floatLiteral(parameterFloat(effect, *definition, "scale"));
            const QString coverage = floatLiteral(parameterFloat(effect, *definition, "coverage"));
            out += QString("    // %1\n"
                           "    float %2_nn = BO3BeginnerFbm3(d * (%3 * 2.6) + float3(3.1, 7.7, 11.3));\n"
                           "    float %2_nm = smoothstep(lerp(0.72, 0.30, %4), 0.86, %2_nn);\n"
                           "    %2_nm *= smoothstep(-0.05, 0.20, d.z);\n"
                           "    float3 %2_nc = lerp(%5, %6, saturate(%2_nn));\n"
                           "    color += %2_nc * (%2_nm * %7);\n")
                .arg(definition->name, tag, scale, coverage, colorLiteral(colorA), colorLiteral(colorB), intensity);
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
    const bool needsFbm3 =
        projectUsesEffect(project, "sky_clouds") ||
        projectUsesEffect(project, "sky_aurora") ||
        projectUsesEffect(project, "sky_nebula");
    const bool needsHash31 =
        (project.target == Target::Material && projectUsesEffect(project, "noise")) ||
        projectUsesEffect(project, "dissolve") ||
        projectUsesEffect(project, "sky_stars") ||
        needsFbm3;
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

    if(needsFbm3)
    {
        out += QStringLiteral(R"HLSL(
float BO3BeginnerValueNoise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = BO3BeginnerHash31(i + float3(0.0, 0.0, 0.0));
    float n100 = BO3BeginnerHash31(i + float3(1.0, 0.0, 0.0));
    float n010 = BO3BeginnerHash31(i + float3(0.0, 1.0, 0.0));
    float n110 = BO3BeginnerHash31(i + float3(1.0, 1.0, 0.0));
    float n001 = BO3BeginnerHash31(i + float3(0.0, 0.0, 1.0));
    float n101 = BO3BeginnerHash31(i + float3(1.0, 0.0, 1.0));
    float n011 = BO3BeginnerHash31(i + float3(0.0, 1.0, 1.0));
    float n111 = BO3BeginnerHash31(i + float3(1.0, 1.0, 1.0));
    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    return lerp(lerp(nx00, nx10, f.y), lerp(nx01, nx11, f.y), f.z);
}

float BO3BeginnerFbm3(float3 p)
{
    float value = 0.0;
    float amplitude = 0.53;
    value += BO3BeginnerValueNoise3(p) * amplitude; p = p * 2.03 + 17.17; amplitude *= 0.50;
    value += BO3BeginnerValueNoise3(p) * amplitude; p = p * 2.01 + 11.31; amplitude *= 0.50;
    value += BO3BeginnerValueNoise3(p) * amplitude; p = p * 2.07 + 7.73; amplitude *= 0.50;
    value += BO3BeginnerValueNoise3(p) * amplitude;
    return saturate(value / 0.99375);
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
    float2 beginnerPixel = input.position.xy;
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
        EffectDef("film_grain", "Film Grain", "Add fine per-pixel moving film grain instead of broad digital bands.", "Atmosphere",
                  {Target::PostFx},
                  {FloatParam("amount", "Strength", "How visible the grain is.", 0.0, 0.20, 0.002, 0.035),
                   FloatParam("scale", "Grain Size", "Size of each grain speck in screen pixels.", 0.5, 4.0, 0.05, 1.15),
                   FloatParam("speed", "Speed", "How quickly the grain pattern changes.", 0.0, 4.0, 0.01, 1.0),
                   FloatParam("shadow_boost", "Shadow Grain", "Increase grain in dark areas like real film stock.", 0.0, 1.0, 0.01, 0.55),
                   FloatParam("color_grain", "Color Grain", "0 is monochrome grain; higher values add subtle RGB grain.", 0.0, 1.0, 0.01, 0.10)}),

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

        EffectDef("sky_sun", "Sun & Glow", "Place a procedural sun anywhere in the sky with adjustable size, color and glow.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Sun Color", "Color of the sun and its halo.", "#FFD89A"),
                   FloatParam("azimuth", "Horizontal Position", "Rotate the sun around the horizon in degrees.", 0.0, 360.0, 1.0, 255.0),
                   FloatParam("elevation", "Height", "Sun height above the horizon in degrees.", -8.0, 90.0, 0.5, 18.0),
                   FloatParam("size", "Disc Size", "Angular radius of the visible sun disc.", 0.25, 12.0, 0.05, 2.2),
                   FloatParam("softness", "Edge Softness", "Softness around the sun disc edge.", 0.05, 4.0, 0.05, 0.45),
                   FloatParam("brightness", "Brightness", "Brightness of the sun disc.", 0.0, 8.0, 0.05, 3.0),
                   FloatParam("glow", "Glow", "Strength of the surrounding halo.", 0.0, 5.0, 0.05, 1.1)}),
        EffectDef("sky_moon", "Moon & Halo", "Place a procedural moon in the sky independently from the sun.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Moon Color", "Color of the moon and halo.", "#DDE8FF"),
                   FloatParam("azimuth", "Horizontal Position", "Rotate the moon around the horizon in degrees.", 0.0, 360.0, 1.0, 70.0),
                   FloatParam("elevation", "Height", "Moon height above the horizon in degrees.", -8.0, 90.0, 0.5, 42.0),
                   FloatParam("size", "Disc Size", "Angular radius of the moon disc.", 0.25, 10.0, 0.05, 1.7),
                   FloatParam("softness", "Edge Softness", "Softness around the disc edge.", 0.05, 4.0, 0.05, 0.25),
                   FloatParam("brightness", "Brightness", "Brightness of the moon disc.", 0.0, 6.0, 0.05, 1.8),
                   FloatParam("halo", "Halo", "Strength of the subtle moon halo.", 0.0, 3.0, 0.05, 0.35)}),
        EffectDef("sky_clouds", "Procedural Clouds", "Generate seamless animated clouds around the sky without any texture files.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Cloud Color", "Main color of the cloud layer.", "#E8EDF2"),
                   FloatParam("coverage", "Coverage", "How much of the sky is covered by clouds.", 0.0, 1.0, 0.01, 0.48),
                   FloatParam("opacity", "Opacity", "How strongly clouds cover the sky behind them.", 0.0, 1.0, 0.01, 0.72),
                   FloatParam("scale", "Cloud Size", "Higher values create smaller cloud structures.", 0.5, 10.0, 0.05, 2.2),
                   FloatParam("softness", "Softness", "How soft the cloud edges are.", 0.025, 0.35, 0.005, 0.11),
                   FloatParam("wind_direction", "Wind Direction", "Direction the clouds travel in degrees.", 0.0, 360.0, 1.0, 28.0),
                   FloatParam("speed", "Wind Speed", "How quickly clouds move across the sky.", -2.0, 2.0, 0.01, 0.08)}),
        EffectDef("sky_mountains", "Mountain Silhouettes", "Build layered procedural mountain ranges around the horizon with no image texture.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("near_color", "Near Mountains", "Color of the closest ridge.", "#101523"),
                   ColorParam("far_color", "Distant Mountains", "Color of the distant ridge.", "#344058"),
                   FloatParam("height", "Height", "Average mountain height above the horizon.", 0.01, 0.38, 0.005, 0.10),
                   FloatParam("roughness", "Jaggedness", "How dramatic the peaks and valleys become.", 0.0, 1.5, 0.01, 0.78),
                   FloatParam("depth", "Distant Layer", "Visibility of the distant mountain layer.", 0.0, 1.0, 0.01, 0.72)}),
        EffectDef("sky_stars", "Procedural Stars", "Fill the upper sky with seamless stars and optional animated twinkle.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Star Color", "Color of the stars.", "#DCEBFF"),
                   FloatParam("density", "Density", "Number of visible stars.", 0.0, 1.0, 0.01, 0.28),
                   FloatParam("scale", "Star Size", "Higher values produce finer, smaller stars.", 90.0, 900.0, 1.0, 360.0),
                   FloatParam("brightness", "Brightness", "How bright the stars appear.", 0.0, 5.0, 0.05, 1.5),
                   FloatParam("twinkle", "Twinkle", "How strongly star brightness animates.", 0.0, 1.0, 0.01, 0.25),
                   FloatParam("twinkle_speed", "Twinkle Speed", "How quickly the stars shimmer.", 0.0, 4.0, 0.01, 0.55)}),
        EffectDef("sky_haze", "Horizon Haze", "Add colored atmospheric haze around the entire horizon.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color", "Haze Color", "Color of the horizon haze.", "#F4A67B"),
                   FloatParam("intensity", "Intensity", "How strongly the haze affects the horizon.", 0.0, 1.0, 0.01, 0.28),
                   FloatParam("width", "Width", "Vertical thickness of the haze band.", 0.02, 0.60, 0.01, 0.16)}),
        EffectDef("sky_aurora", "Aurora Ribbons", "Add animated procedural aurora ribbons across the upper sky.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color_a", "Primary Color", "Primary aurora color.", "#54FFC4"),
                   ColorParam("color_b", "Secondary Color", "Secondary aurora color.", "#6C75FF"),
                   FloatParam("intensity", "Intensity", "Brightness of the aurora.", 0.0, 4.0, 0.05, 0.85),
                   FloatParam("scale", "Ribbon Detail", "Complexity and width of the ribbons.", 0.5, 8.0, 0.05, 2.2),
                   FloatParam("speed", "Movement Speed", "How quickly the aurora flows.", -2.0, 2.0, 0.01, 0.10)}),
        EffectDef("sky_nebula", "Nebula / Space Clouds", "Add colorful procedural space fog for fantasy, alien and deep-space skies.", "Sky & Environment",
                  {Target::Sky},
                  {ColorParam("color_a", "Color A", "First nebula color.", "#6D3EFF"),
                   ColorParam("color_b", "Color B", "Second nebula color.", "#FF4FC3"),
                   FloatParam("intensity", "Intensity", "Brightness of the nebula.", 0.0, 3.0, 0.05, 0.60),
                   FloatParam("scale", "Cloud Size", "Higher values create smaller space-cloud structures.", 0.5, 10.0, 0.05, 2.1),
                   FloatParam("coverage", "Coverage", "How much of the sky receives nebula color.", 0.0, 1.0, 0.01, 0.42)}),

        EffectDef("gradient", "Color Gradient", "Blend a top and bottom color through the screen or sky while keeping its detail.", "Patterns",
                  {Target::PostFx, Target::Sky},
                  {ColorParam("bottom_color", "Bottom Color", "Color toward the bottom.", "#5740A8"),
                   ColorParam("top_color", "Top Color", "Color toward the top.", "#63D8D0"),
                   FloatParam("strength", "Strength", "How strongly the gradient colors the result.", 0.0, 1.0, 0.01, 0.40)}),
        EffectDef("grid_rings", "Grid / Rings", "Overlay a procedural grid or rings without requiring an image texture.", "Patterns",
                  {Target::PostFx, Target::Material},
                  {ColorParam("color", "Pattern Color", "Color of the lines.", "#6FE8FF"),
                   FloatParam("strength", "Strength", "Brightness of the pattern.", 0.0, 3.0, 0.01, 0.22),
                   FloatParam("scale", "Scale", "How many pattern cells or rings are visible.", 2.0, 80.0, 0.1, 12.0),
                   FloatParam("rings", "Grid / Rings Mix", "0 is all grid; 1 is all rings; values between blend both.", 0.0, 1.0, 0.01, 0.0)})
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
    return {{"blank", "Blank Sky"}, {"sunset", "Sunset"}, {"dream_sky", "Dream Sky"},
            {"mountain_dawn", "Mountain Dawn"}, {"starry_night", "Starry Night"}, {"aurora_night", "Aurora Night"}};
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
        add("film_grain", {{"amount", 0.030}, {"scale", 1.10}, {"speed", 1.0}, {"shadow_boost", 0.55}, {"color_grain", 0.08}});
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
        add("sky_haze", {{"color", "#FF9A6A"}, {"intensity", 0.42}, {"width", 0.18}});
        add("sky_sun", {{"color", "#FFD08A"}, {"azimuth", 265.0}, {"elevation", 9.0}, {"size", 2.6}, {"brightness", 3.4}, {"glow", 1.5}});
        add("sky_clouds", {{"color", "#E8B6A2"}, {"coverage", 0.34}, {"opacity", 0.32}, {"scale", 2.0}, {"speed", 0.045}});
    }
    else if(target == Target::Sky && id == "dream_sky")
    {
        project.name = "Dream Sky";
        project.settings["zenithColor"] = "#24124F";
        project.settings["horizonColor"] = "#B85BBE";
        project.settings["groundColor"] = "#070814";
        add("tint", {{"color", "#8FA7FF"}, {"amount", 0.22}});
        add("saturation", {{"amount", 1.32}});
        add("sky_stars", {{"density", 0.34}, {"brightness", 1.75}, {"twinkle", 0.34}, {"twinkle_speed", 0.48}});
        add("sky_nebula", {{"color_a", "#5B46FF"}, {"color_b", "#E65EC7"}, {"intensity", 0.48}, {"coverage", 0.38}});
        add("pulse", {{"amount", 0.035}, {"speed", 0.18}});
    }
    else if(target == Target::Sky && id == "mountain_dawn")
    {
        project.name = "Mountain Dawn";
        project.settings["zenithColor"] = "#274B7A";
        project.settings["horizonColor"] = "#F2A47D";
        project.settings["groundColor"] = "#10151D";
        add("sky_haze", {{"color", "#F7B08A"}, {"intensity", 0.36}, {"width", 0.20}});
        add("sky_sun", {{"color", "#FFE0A3"}, {"azimuth", 235.0}, {"elevation", 12.0}, {"size", 2.1}, {"brightness", 2.8}, {"glow", 1.0}});
        add("sky_clouds", {{"color", "#D9E1E8"}, {"coverage", 0.30}, {"opacity", 0.34}, {"scale", 2.4}, {"speed", 0.035}});
        add("sky_mountains", {{"near_color", "#151A22"}, {"far_color", "#48566A"}, {"height", 0.11}, {"roughness", 0.88}, {"depth", 0.78}});
    }
    else if(target == Target::Sky && id == "starry_night")
    {
        project.name = "Starry Night";
        project.settings["zenithColor"] = "#050B22";
        project.settings["horizonColor"] = "#17254A";
        project.settings["groundColor"] = "#02040A";
        add("sky_stars", {{"density", 0.58}, {"brightness", 2.1}, {"scale", 430.0}, {"twinkle", 0.42}, {"twinkle_speed", 0.65}});
        add("sky_moon", {{"color", "#E5EEFF"}, {"azimuth", 72.0}, {"elevation", 48.0}, {"size", 1.8}, {"brightness", 2.0}, {"halo", 0.42}});
        add("sky_haze", {{"color", "#28385E"}, {"intensity", 0.18}, {"width", 0.15}});
    }
    else if(target == Target::Sky && id == "aurora_night")
    {
        project.name = "Aurora Night";
        project.settings["zenithColor"] = "#04091B";
        project.settings["horizonColor"] = "#102B3A";
        project.settings["groundColor"] = "#01040A";
        add("sky_stars", {{"density", 0.30}, {"brightness", 1.35}, {"scale", 380.0}, {"twinkle", 0.18}});
        add("sky_aurora", {{"color_a", "#54FFC4"}, {"color_b", "#6D79FF"}, {"intensity", 1.05}, {"scale", 2.4}, {"speed", 0.08}});
        add("sky_mountains", {{"near_color", "#060A10"}, {"far_color", "#16232D"}, {"height", 0.085}, {"roughness", 0.72}, {"depth", 0.62}});
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
