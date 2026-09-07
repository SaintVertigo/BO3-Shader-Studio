#include "bo3_package_adapter.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <functional>

namespace bo3
{
namespace
{

void lowerConfidence(AutomationConfidence& current, AutomationConfidence value)
{
    if(static_cast<int>(value) > static_cast<int>(current)) current = value;
}

bool isConvertedShadertoySource(const QString& source)
{
    return source.contains("Shadertoy per-pass iChannel bindings", Qt::CaseInsensitive) ||
           source.contains(QRegularExpression(
               R"(\biChannel[0-3]\b[\s\S]*\bGLSL_(?:TEXTURE|TEXEL_FETCH))",
               QRegularExpression::CaseInsensitiveOption));
}

int shadertoyChannelIndex(const QString& name)
{
    const QRegularExpressionMatch match = QRegularExpression(
        R"(^iChannel([0-3])$)", QRegularExpression::CaseInsensitiveOption).match(name);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

bool hasUnresolvedShadertoyBufferDependency(const QString& source, QString* detail = nullptr)
{
    const QRegularExpression re(
        R"(^\s*//\s*iChannel([0-3])\s*<-\s*(Buffer\s+[A-D])\b)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(source);
    if(!match.hasMatch()) return false;
    if(detail) *detail = QString("iChannel%1 <- %2").arg(match.captured(1), match.captured(2));
    return true;
}

QString normalizeConvertedShadertoySamplers(
    QString& source, ValidationResult& diagnostics, bool& adaptedSourceRequired)
{
    if(!isConvertedShadertoySource(source)) return {};

    // Converter-created glslSampler/glslSamplerN names are legal HLSL but are
    // not a proven BO3 material/linker parameter contract. The older export
    // path already normalizes these to a BO3-known sampler; live Preview As
    // must do the same or it can report a false-positive package PASS.
    const QRegularExpression syntheticDeclRe(
        R"(\bSamplerState\s+(glslSampler[0-3]*)\s*(?::\s*register\s*\(\s*s[0-9]+\s*\))?\s*;)",
        QRegularExpression::CaseInsensitiveOption);
    auto it = syntheticDeclRe.globalMatch(source);
    QStringList syntheticNames;
    QVector<QPair<int, int>> declarationSpans;
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        syntheticNames << match.captured(1);
        declarationSpans.push_back({match.capturedStart(), match.capturedLength()});
    }
    syntheticNames.removeDuplicates();
    if(syntheticNames.isEmpty()) return {};

    const ShaderSourceAnalysis liveResources = analyzeShaderSource(source);
    bool hasBilinear = false;
    bool hasColor = false;
    for(const SourceResource& sampler : liveResources.samplers)
    {
        hasBilinear = hasBilinear || sampler.name.compare(
            "bilinearClampler", Qt::CaseInsensitive) == 0;
        hasColor = hasColor || sampler.name.compare(
            "colorSampler", Qt::CaseInsensitive) == 0;
    }
    const QString target = hasBilinear ? QString("bilinearClampler")
        : (hasColor ? QString("colorSampler") : QString("bilinearClampler"));
    const bool targetAlreadyDeclared = hasBilinear || hasColor;

    // Remove the synthetic declarations before token replacement so multiple
    // glslSamplerN declarations cannot collapse into duplicate declarations of
    // the same target sampler at different registers.
    for(int index = declarationSpans.size() - 1; index >= 0; --index)
        source.remove(declarationSpans[index].first, declarationSpans[index].second);
    for(const QString& name : syntheticNames)
    {
        source.replace(QRegularExpression(
            QString(R"(\b%1\b)").arg(QRegularExpression::escape(name)),
            QRegularExpression::CaseInsensitiveOption), target);
    }

    if(!targetAlreadyDeclared)
    {
        const QString declaration = QString("SamplerState %1 : register(s1);\n").arg(target);
        const QRegularExpression includeRe(
            R"(#\s*include\s*[<\"]postfx/postfx_common\.h[>\"]\s*)",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch includeMatch = includeRe.match(source);
        if(includeMatch.hasMatch()) source.insert(includeMatch.capturedEnd(), declaration);
        else source.prepend(declaration);
    }

    adaptedSourceRequired = true;
    diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_SAMPLER_NORMALIZED",
        QString("Normalized converter-generated sampler(s) %1 to proven BO3 sampler '%2'.")
            .arg(syntheticNames.join(", "), target));
    return target;
}

PackageResourceRole postFxTextureRole(const SourceResource& texture,
                                      const QString& source,
                                      QString& reason)
{
    const QString lower = texture.name.toLower();
    const int shadertoyChannel = shadertoyChannelIndex(texture.name);
    if(shadertoyChannel >= 0 && isConvertedShadertoySource(source))
    {
        if(shadertoyChannel == 0)
        {
            reason = "Converted Shadertoy iChannel0 is the primary PostFX scene input by default.";
            return PackageResourceRole::ResolvedScene;
        }
        reason = "Converted Shadertoy auxiliary channels default to material images; duplicate source-image channels may be promoted to resolvedScene by the live preview session.";
        return PackageResourceRole::MaterialImage;
    }
    if(lower == "framebuffer" || lower == "resolvedscene" ||
       (texture.bindPoint == 0 && source.contains("PostFx_NormalizeColor")))
    {
        reason = "The primary color input is a proven resolvedScene PostFX pattern.";
        return PackageResourceRole::ResolvedScene;
    }
    if(lower == "depthsampler" || lower == "floatz" || lower == "scenedepth" ||
       lower == "depthtexture")
    {
        reason = "The reflected depth resource matches the BO3 floatZ CodeTexture contract.";
        return PackageResourceRole::FloatDepth;
    }
    reason = "No proven BO3 runtime CodeTexture mapping was found for this texture.";
    return PackageResourceRole::Unknown;
}

PackageResourceRole roleFromChoice(const QString& choice)
{
    if(choice.compare("resolvedScene", Qt::CaseInsensitive) == 0) return PackageResourceRole::ResolvedScene;
    if(choice.compare("floatZ", Qt::CaseInsensitive) == 0) return PackageResourceRole::FloatDepth;
    if(choice.compare("materialImage", Qt::CaseInsensitive) == 0) return PackageResourceRole::MaterialImage;
    if(choice.compare("ignore", Qt::CaseInsensitive) == 0) return PackageResourceRole::Ignore;
    return PackageResourceRole::Unknown;
}

bool isKnownSampler(const QString& name)
{
    static const QSet<QString> known{
        "bilinearclampler", "pointclampler", "colorsampler",
        "framebuffersampler", "depthsamplerstate"
    };
    return known.contains(name.toLower());
}

void ensurePostFxInclude(QString& source);

bool convertedShadertoySceneNeedsYFlip(const QString& source)
{
    const QRegularExpression marker(
        R"(^\s*//\s*BO3_PREVIEWER_POSTFX_SCENE_ORIENTATION\s*:\s*(FLIP_Y|KEEP_Y)\s*$)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = marker.match(source);
    if(match.hasMatch())
        return match.captured(1).compare("KEEP_Y", Qt::CaseInsensitive) != 0;

    // Legacy converted Shadertoy HLSL predates the explicit marker. Preserve
    // the old behavior for those files so previously working paint/PostFX
    // packages keep their known lower-left -> upper-left bridge.
    return true;
}


int findMatchingDelimiter(const QString& text, int openPos, QChar openChar, QChar closeChar)
{
    if(openPos < 0 || openPos >= text.size() || text.at(openPos) != openChar) return -1;
    int depth = 0;
    for(int i = openPos; i < text.size(); ++i)
    {
        const QChar c = text.at(i);
        if(c == openChar) ++depth;
        else if(c == closeChar && --depth == 0) return i;
    }
    return -1;
}

QStringList splitTopLevelArguments(const QString& arguments)
{
    QStringList result;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int start = 0;
    for(int i = 0; i < arguments.size(); ++i)
    {
        const QChar c = arguments.at(i);
        if(c == '(') ++parenDepth;
        else if(c == ')') --parenDepth;
        else if(c == '[') ++bracketDepth;
        else if(c == ']') --bracketDepth;
        else if(c == '{') ++braceDepth;
        else if(c == '}') --braceDepth;
        else if(c == ',' && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0)
        {
            result << arguments.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    result << arguments.mid(start).trimmed();
    return result;
}

QString stripSimpleOuterParens(QString value)
{
    value = value.trimmed();
    while(value.size() >= 2 && value.front() == '(' && value.back() == ')')
    {
        const int close = findMatchingDelimiter(value, 0, QChar('('), QChar(')'));
        if(close != value.size() - 1) break;
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value;
}

bool isResolvedSceneArgument(const QString& argument, const QSet<QString>& resolvedNames)
{
    const QString value = stripSimpleOuterParens(argument);
    for(const QString& resolved : resolvedNames)
    {
        if(value.compare(resolved, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

void rewriteSceneTextureAccess(QString& target, const QString& textureName)
{
    const QString escaped = QRegularExpression::escape(textureName);
    auto replaceMacroPrefix = [&](const QString& macro, const QString& replacement)
    {
        target.replace(QRegularExpression(
            QString(R"(\b%1\s*\(\s*%2\s*,)").arg(macro, escaped)),
            replacement + "(" + textureName + ",");
    };
    replaceMacroPrefix("GLSL_TEXTURE", "BO3_SCENE_TEXTURE");
    replaceMacroPrefix("GLSL_TEXTURE_BIAS", "BO3_SCENE_TEXTURE_BIAS");
    replaceMacroPrefix("GLSL_TEXTURE_LEVEL", "BO3_SCENE_TEXTURE_LEVEL");
    replaceMacroPrefix("GLSL_TEXTURE_GRAD", "BO3_SCENE_TEXTURE_GRAD");
    replaceMacroPrefix("GLSL_TEXTURE_S", "BO3_SCENE_TEXTURE_S");
    replaceMacroPrefix("GLSL_TEXTURE_BIAS_S", "BO3_SCENE_TEXTURE_BIAS_S");
    replaceMacroPrefix("GLSL_TEXTURE_LEVEL_S", "BO3_SCENE_TEXTURE_LEVEL_S");
    replaceMacroPrefix("GLSL_TEXTURE_GRAD_S", "BO3_SCENE_TEXTURE_GRAD_S");
    replaceMacroPrefix("GLSL_TEXEL_FETCH", "BO3_SCENE_TEXEL_FETCH");

    auto replaceMethodPrefix = [&](const QString& method, const QString& replacement)
    {
        target.replace(QRegularExpression(
            QString(R"(\b%1\s*\.\s*%2\s*\()").arg(escaped, method)),
            replacement + "(" + textureName + ",");
    };
    replaceMethodPrefix("Sample", "BO3_SCENE_SAMPLE");
    replaceMethodPrefix("SampleBias", "BO3_SCENE_SAMPLE_BIAS");
    replaceMethodPrefix("SampleLevel", "BO3_SCENE_SAMPLE_LEVEL");
    replaceMethodPrefix("SampleGrad", "BO3_SCENE_SAMPLE_GRAD");
    replaceMethodPrefix("Load", "BO3_SCENE_LOAD");
}

QStringList propagateResolvedSceneThroughTextureHelpers(
    QString& source, const QSet<QString>& resolvedNames)
{
    QStringList adaptedHelpers;
    if(resolvedNames.isEmpty()) return adaptedHelpers;

    // Converted GLSL frequently hides iChannel0 behind helpers such as:
    //   float3 tex2D(Texture2D<float4> tex, float2 uv) { return GLSL_TEXTURE(tex, uv).xyz; }
    // A direct resource-name rewrite cannot see that `tex` is resolvedScene at
    // runtime. Discover Texture2D helper parameters and specialize/rewrite them
    // when call sites prove that the parameter receives a resolvedScene channel.
    const QRegularExpression helperHeaderRe(
        R"(\b[A-Za-z_]\w*(?:\s*<[^>\n]+>)?\s+([A-Za-z_]\w*)\s*\(([^\n\r{};]*\bTexture2D(?:\s*<[^>]+>)?[^\n\r{};]*)\)\s*\{)",
        QRegularExpression::CaseInsensitiveOption);

    struct HelperCandidate
    {
        QString name;
        QString parameterName;
        int parameterIndex = -1;
        QString functionText;
    };
    QVector<HelperCandidate> candidates;

    auto helperIt = helperHeaderRe.globalMatch(source);
    while(helperIt.hasNext())
    {
        const QRegularExpressionMatch header = helperIt.next();
        const QString name = header.captured(1);
        const QString params = header.captured(2);
        const int openBrace = header.capturedEnd() - 1;
        const int closeBrace = findMatchingDelimiter(source, openBrace, QChar('{'), QChar('}'));
        if(closeBrace < 0) continue;

        const QStringList paramList = splitTopLevelArguments(params);
        for(int paramIndex = 0; paramIndex < paramList.size(); ++paramIndex)
        {
            const QRegularExpression textureParamRe(
                R"(\bTexture2D(?:\s*<[^>]+>)?\s+([A-Za-z_]\w*)\b)",
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch textureParam = textureParamRe.match(paramList.at(paramIndex));
            if(!textureParam.hasMatch()) continue;

            HelperCandidate candidate;
            candidate.name = name;
            candidate.parameterName = textureParam.captured(1);
            candidate.parameterIndex = paramIndex;
            candidate.functionText = source.mid(header.capturedStart(), closeBrace - header.capturedStart() + 1);
            candidates.push_back(candidate);
        }
    }

    for(const HelperCandidate& candidate : candidates)
    {
        // Re-find the original function after earlier candidates may have
        // changed source length. The exact generated function text is stable.
        const int functionPos = source.indexOf(candidate.functionText);
        if(functionPos < 0) continue;
        const int functionEnd = functionPos + candidate.functionText.size();

        struct CallSite { qsizetype nameStart = -1; bool resolved = false; };
        QVector<CallSite> calls;
        const QRegularExpression callRe(
            QString(R"(\b%1\s*\()").arg(QRegularExpression::escape(candidate.name)),
            QRegularExpression::CaseInsensitiveOption);
        auto callIt = callRe.globalMatch(source);
        while(callIt.hasNext())
        {
            const QRegularExpressionMatch call = callIt.next();
            if(call.capturedStart() >= functionPos && call.capturedStart() < functionEnd)
                continue; // Function declaration (or a recursive call inside it).
            const int openParen = source.indexOf('(', call.capturedStart());
            const int closeParen = findMatchingDelimiter(source, openParen, QChar('('), QChar(')'));
            if(openParen < 0 || closeParen < 0) continue;
            const QStringList args = splitTopLevelArguments(source.mid(openParen + 1, closeParen - openParen - 1));
            if(candidate.parameterIndex < 0 || candidate.parameterIndex >= args.size()) continue;
            calls.push_back({call.capturedStart(), isResolvedSceneArgument(args.at(candidate.parameterIndex), resolvedNames)});
        }
        if(calls.isEmpty()) continue;

        bool hasResolved = false;
        bool hasOther = false;
        for(const CallSite& call : calls)
        {
            hasResolved = hasResolved || call.resolved;
            hasOther = hasOther || !call.resolved;
        }
        if(!hasResolved) continue;

        if(!hasOther)
        {
            QString rewritten = candidate.functionText;
            rewriteSceneTextureAccess(rewritten, candidate.parameterName);
            if(rewritten == candidate.functionText) continue;
            source.replace(functionPos, candidate.functionText.size(), rewritten);
            adaptedHelpers << candidate.name;
            continue;
        }

        // Mixed helper: preserve the ordinary-image path and create a scene-only
        // specialization. Only calls that pass a resolvedScene channel are
        // redirected to the clone.
        QString cloneName = candidate.name + QString("_BO3ResolvedScene_%1").arg(candidate.parameterIndex);
        QString clone = candidate.functionText;
        rewriteSceneTextureAccess(clone, candidate.parameterName);
        if(clone == candidate.functionText) continue;
        const QRegularExpression cloneNameRe(
            QString(R"(\b%1\s*\()").arg(QRegularExpression::escape(candidate.name)),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch cloneNameMatch = cloneNameRe.match(clone);
        if(!cloneNameMatch.hasMatch()) continue;
        clone.replace(cloneNameMatch.capturedStart(), candidate.name.size(), cloneName);

        QVector<qsizetype> resolvedCallStarts;
        for(const CallSite& call : calls)
            if(call.resolved) resolvedCallStarts.push_back(call.nameStart);
        std::sort(resolvedCallStarts.begin(), resolvedCallStarts.end(), std::greater<qsizetype>());
        for(qsizetype start : resolvedCallStarts)
            source.replace(start, candidate.name.size(), cloneName);

        const int refreshedFunctionPos = source.indexOf(candidate.functionText);
        if(refreshedFunctionPos >= 0)
            source.insert(refreshedFunctionPos + candidate.functionText.size(), "\n\n" + clone);
        adaptedHelpers << candidate.name;
    }

    adaptedHelpers.removeDuplicates();
    return adaptedHelpers;
}

void adaptConvertedShadertoyColorDomain(
    QString& source, const QVector<PackageResourceMapping>& mappings,
    ValidationResult& diagnostics, bool& adaptedSourceRequired)
{
    if(!isConvertedShadertoySource(source) ||
       source.contains("BO3_PREVIEWER_SCENE_CHANNEL_NORMALIZATION"))
        return;

    ensurePostFxInclude(source);
    const bool flipSceneY = convertedShadertoySceneNeedsYFlip(source);
    QString bridge = QStringLiteral(R"BO3DOMAIN(
// BO3_PREVIEWER_SCENE_CHANNEL_NORMALIZATION
// Converted Shadertoy math is authored in ordinary 0..1 color space. BO3
// resolvedScene is stored at the PostFX HDR scale in Runtime, while TOOLSGFX is
// identity. Normalize only channels mapped to resolvedScene and restore the
// engine scale once at the final pixel output.
float4 BO3_PostFxNormalize4(float4 c) { return float4(PostFx_NormalizeColor(c.rgb), c.a); }
// Shadertoy fragment coordinates use a lower-left convention, while BO3/D3D
// resolvedScene is upper-left. The converter records whether the authored GLSL
// already inverted its scene UV. This bridge is therefore either one boundary
// Y flip or an identity mapping; procedural fragCoord orientation is unchanged.
float2 BO3_PostFxSceneUV(float2 uv) { return float2(uv.x, 1.0 - uv.y); }
float2 BO3_PostFxSceneGrad(float2 g) { return float2(g.x, -g.y); }
int2 BO3_PostFxSceneTexel(int2 p) { return int2(p.x, max(0, (int)PostFx_GetRenderTargetSize().y - 1 - p.y)); }
int3 BO3_PostFxSceneLoadCoord(int3 p) { return int3(p.x, max(0, (int)PostFx_GetRenderTargetSize().y - 1 - p.y), p.z); }
#define BO3_POSTFX_NORMALIZE4(c) BO3_PostFxNormalize4((c))
#define BO3_SCENE_TEXTURE(tex,uv) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE(tex,BO3_PostFxSceneUV(uv)))
#define BO3_SCENE_TEXTURE_BIAS(tex,uv,bias) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_BIAS(tex,BO3_PostFxSceneUV(uv),bias))
#define BO3_SCENE_TEXTURE_LEVEL(tex,uv,lod) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_LEVEL(tex,BO3_PostFxSceneUV(uv),lod))
#define BO3_SCENE_TEXTURE_GRAD(tex,uv,dx,dy) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_GRAD(tex,BO3_PostFxSceneUV(uv),BO3_PostFxSceneGrad(dx),BO3_PostFxSceneGrad(dy)))
#define BO3_SCENE_TEXTURE_S(tex,samp,uv) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_S(tex,samp,BO3_PostFxSceneUV(uv)))
#define BO3_SCENE_TEXTURE_BIAS_S(tex,samp,uv,bias) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_BIAS_S(tex,samp,BO3_PostFxSceneUV(uv),bias))
#define BO3_SCENE_TEXTURE_LEVEL_S(tex,samp,uv,lod) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_LEVEL_S(tex,samp,BO3_PostFxSceneUV(uv),lod))
#define BO3_SCENE_TEXTURE_GRAD_S(tex,samp,uv,dx,dy) BO3_POSTFX_NORMALIZE4(GLSL_TEXTURE_GRAD_S(tex,samp,BO3_PostFxSceneUV(uv),BO3_PostFxSceneGrad(dx),BO3_PostFxSceneGrad(dy)))
#define BO3_SCENE_TEXEL_FETCH(tex,p,lod) BO3_POSTFX_NORMALIZE4(GLSL_TEXEL_FETCH(tex,BO3_PostFxSceneTexel(int2(p)),lod))
#define BO3_SCENE_SAMPLE(tex,samp,uv) BO3_POSTFX_NORMALIZE4((tex).Sample((samp),BO3_PostFxSceneUV(uv)))
#define BO3_SCENE_SAMPLE_BIAS(tex,samp,uv,bias) BO3_POSTFX_NORMALIZE4((tex).SampleBias((samp),BO3_PostFxSceneUV(uv),(bias)))
#define BO3_SCENE_SAMPLE_LEVEL(tex,samp,uv,lod) BO3_POSTFX_NORMALIZE4((tex).SampleLevel((samp),BO3_PostFxSceneUV(uv),(lod)))
#define BO3_SCENE_SAMPLE_GRAD(tex,samp,uv,dx,dy) BO3_POSTFX_NORMALIZE4((tex).SampleGrad((samp),BO3_PostFxSceneUV(uv),BO3_PostFxSceneGrad(dx),BO3_PostFxSceneGrad(dy)))
#define BO3_SCENE_LOAD(tex,p) BO3_POSTFX_NORMALIZE4((tex).Load(BO3_PostFxSceneLoadCoord(int3(p))))

)BO3DOMAIN");
    if(!flipSceneY)
    {
        bridge.replace("float2 BO3_PostFxSceneUV(float2 uv) { return float2(uv.x, 1.0 - uv.y); }",
                       "float2 BO3_PostFxSceneUV(float2 uv) { return uv; }");
        bridge.replace("float2 BO3_PostFxSceneGrad(float2 g) { return float2(g.x, -g.y); }",
                       "float2 BO3_PostFxSceneGrad(float2 g) { return g; }");
        bridge.replace("int2 BO3_PostFxSceneTexel(int2 p) { return int2(p.x, max(0, (int)PostFx_GetRenderTargetSize().y - 1 - p.y)); }",
                       "int2 BO3_PostFxSceneTexel(int2 p) { return p; }");
        bridge.replace("int3 BO3_PostFxSceneLoadCoord(int3 p) { return int3(p.x, max(0, (int)PostFx_GetRenderTargetSize().y - 1 - p.y), p.z); }",
                       "int3 BO3_PostFxSceneLoadCoord(int3 p) { return p; }");
    }

    // Keep the include first for readable generated HLSL.
    const QRegularExpression includeRe(
        R"(#\s*include\s*[<\"]postfx/postfx_common\.h[>\"]\s*)",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch includeMatch = includeRe.match(source);
    if(includeMatch.hasMatch())
        source.insert(includeMatch.capturedEnd(), bridge);
    else
        source.prepend(bridge);

    QSet<QString> resolvedSceneNames;
    QStringList normalizedChannels;
    for(const PackageResourceMapping& mapping : mappings)
    {
        if(mapping.resourceKind != CompiledResourceKind::Texture ||
           mapping.role != PackageResourceRole::ResolvedScene)
            continue;
        resolvedSceneNames.insert(mapping.resourceName);
        normalizedChannels << mapping.resourceName;
    }

    const QStringList helperBridges =
        propagateResolvedSceneThroughTextureHelpers(source, resolvedSceneNames);
    for(const QString& name : resolvedSceneNames)
        rewriteSceneTextureAccess(source, name);

    if(!helperBridges.isEmpty())
    {
        diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_SCENE_HELPER_PROPAGATED",
            QString("Propagated resolvedScene sampling semantics through converted texture helper(s): %1. Scene orientation and BO3 color normalization now apply even when iChannel0 is forwarded through a Texture2D parameter.")
                .arg(helperBridges.join(", ")));
    }

    // Converter-generated PostFX wrappers return the Shadertoy fragColor in
    // ordinary color space. Restore BO3's Runtime scale once at the final stage;
    // TOOLSGFX remains identity through PostFx_DenormalizeColor().
    const int finalReturn = source.lastIndexOf(QRegularExpression(
        R"(\breturn\s+fragColor\s*;)", QRegularExpression::CaseInsensitiveOption));
    if(finalReturn >= 0)
    {
        const QRegularExpression returnRe(
            R"(\breturn\s+fragColor\s*;)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = returnRe.match(source, finalReturn);
        if(match.hasMatch())
            source.replace(match.capturedStart(), match.capturedLength(),
                "return float4(PostFx_DenormalizeColor(fragColor.rgb), fragColor.a);");
    }

    adaptedSourceRequired = true;
    diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_COLOR_DOMAIN",
        normalizedChannels.isEmpty()
            ? "Added the converted-Shadertoy PostFX output scale bridge; no texture channel is currently mapped to resolvedScene."
            : QString("Added BO3 Runtime color-domain normalization for resolvedScene channel(s): %1, plus final PostFx_DenormalizeColor output scaling.")
                  .arg(normalizedChannels.join(", ")));
    if(!normalizedChannels.isEmpty())
    {
        diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_SCENE_ORIENTATION",
            flipSceneY
                ? QString("Converted Shadertoy resolvedScene channel(s) %1 from lower-left channel UVs to BO3/D3D upper-left texture orientation. Procedural fragCoord orientation is unchanged.")
                      .arg(normalizedChannels.join(", "))
                : QString("Preserved the authored upper-left scene orientation for resolvedScene channel(s) %1; no additional Y flip was applied. Procedural fragCoord orientation is unchanged.")
                      .arg(normalizedChannels.join(", ")));
    }
}

void adaptConvertedShadertoyResolvedSceneGeometry(
    QString& source, const QVector<PackageResourceMapping>& mappings,
    ValidationResult& diagnostics, bool& adaptedSourceRequired)
{
    if(!isConvertedShadertoySource(source) ||
       source.contains("BO3_PREVIEWER_RESOLVED_SCENE_GEOMETRY"))
        return;

    QStringList adjustedChannels;
    for(const PackageResourceMapping& mapping : mappings)
    {
        if(mapping.resourceKind != CompiledResourceKind::Texture ||
           mapping.role != PackageResourceRole::ResolvedScene)
            continue;

        // A Shadertoy image input has its own image dimensions. BO3
        // resolvedScene, however, is the fullscreen render target. If converted
        // code keeps using the dimensions of a user-loaded preview image for a
        // channel that is now resolvedScene, fit/center math can introduce a
        // false zoom, crop or offset. Make textureSize() for resolvedScene
        // channels authoritative to the PostFX render target while leaving
        // auxiliary material images at their native dimensions.
        const QString escaped = QRegularExpression::escape(mapping.resourceName);

        // The GLSL converter can represent vec2(textureSize(...)) as a scalar
        // cast plus splat helper, e.g.
        //   GLSL_VEC2_S((float)(GLSL_TEXTURE_SIZE(iChannel0, 0)))
        // Replacing only the inner textureSize() would leave width,width and
        // still make a 16:9 resolvedScene look square. Collapse the complete
        // converted vec2 expression to the actual PostFX render-target size.
        const QRegularExpression splatSizeRe(
            QString(R"(\bGLSL_VEC2_S\s*\(\s*\(\s*float\s*\)\s*\(\s*GLSL_TEXTURE_SIZE\s*\(\s*%1\s*,\s*[^\)]*\)\s*\)\s*\))").arg(escaped),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression vectorSizeRe(
            QString(R"(\bGLSL_VEC2_V2\s*\(\s*\(\s*float2\s*\)\s*\(\s*GLSL_TEXTURE_SIZE\s*\(\s*%1\s*,\s*[^\)]*\)\s*\)\s*\))").arg(escaped),
            QRegularExpression::CaseInsensitiveOption);
        int replacements = source.count(splatSizeRe) + source.count(vectorSizeRe);
        source.replace(splatSizeRe, "(iResolution.xy)");
        source.replace(vectorSizeRe, "(iResolution.xy)");

        const QRegularExpression sizeRe(
            QString(R"(\bGLSL_TEXTURE_SIZE\s*\(\s*%1\s*,\s*[^\)]*\))").arg(escaped),
            QRegularExpression::CaseInsensitiveOption);
        replacements += source.count(sizeRe);
        source.replace(sizeRe, "int2(iResolution.xy)");
        if(replacements <= 0) continue;
        adjustedChannels << mapping.resourceName;
    }

    if(adjustedChannels.isEmpty()) return;

    // Marker documents that the adapter deliberately touched only the
    // resolvedScene dimension contract. It does not change authored fullscreen
    // coordinates or introduce another vertical flip.
    source.prepend(QString("// BO3_PREVIEWER_RESOLVED_SCENE_GEOMETRY: %1 uses render-target dimensions; authored UV/fragCoord orientation preserved.\n")
                       .arg(adjustedChannels.join(", ")));
    adaptedSourceRequired = true;
    diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_RESOLVED_SCENE_GEOMETRY",
        QString("ResolvedScene channel(s) %1 now report PostFX render-target dimensions, preventing preview-image size/aspect from introducing zoom, crop, stretch or centering offsets. The authored fullscreen coordinate/Y-flip path was preserved unchanged.")
            .arg(adjustedChannels.join(", ")));
}

void ensurePostFxInclude(QString& source)
{
    if(!source.contains(QRegularExpression(
           R"(#\s*include\s*[<\"]postfx/postfx_common\.h[>\"])",
           QRegularExpression::CaseInsensitiveOption)))
        source.prepend("#include \"postfx/postfx_common.h\"\n\n");
}

} // namespace

PackageAdapterResult adaptPostFxPackage(const PackageAdapterRequest& request,
                                        const ShaderSourceAnalysis& analysis)
{
    PackageAdapterResult result;
    result.target = PackageTarget::PostFx;
    result.confidence = AutomationConfidence::Auto;
    result.sourceFileName = QFileInfo(request.sourceFileName).fileName();
    if(result.sourceFileName.isEmpty()) result.sourceFileName = "shader_bo3_postfx.hlsl";
    result.adaptedSource = request.source;
    ShaderSourceAnalysis effectiveAnalysis = analysis;
    if(isConvertedShadertoySource(result.adaptedSource))
    {
        normalizeConvertedShadertoySamplers(
            result.adaptedSource, result.diagnostics, result.adaptedSourceRequired);
        effectiveAnalysis = analyzeShaderSource(result.adaptedSource);
    }

    if(!effectiveAnalysis.hasPixelEntry)
    {
        result.confidence = AutomationConfidence::Unsupported;
        result.diagnostics.add(DiagnosticLevel::Error, "ADAPTER_POSTFX_NO_PS",
            "PostFX packaging requires a ps_main pixel entry point.");
        return result;
    }

    QString bufferDependency;
    if(hasUnresolvedShadertoyBufferDependency(request.source, &bufferDependency))
    {
        result.confidence = AutomationConfidence::Unsupported;
        result.diagnostics.add(DiagnosticLevel::Error,
            "ADAPTER_POSTFX_MULTIPASS_DEPENDENCY",
            QString("This converted Shadertoy pass depends on %1. The current standalone PostFX adapter cannot safely replace an intermediate Buffer A-D output with resolvedScene or a material image. Preview/package the producing buffer pass, or use a future multipass runtime.")
                .arg(bufferDependency));
        return result;
    }

    // Synthetic standalone-preview globals are never carried into a strict
    // package. Only the proven member expressions are replaced, in the adapted
    // copy, leaving the user's authored shader untouched.
    if(!result.adaptedSource.contains(QRegularExpression(
           R"(\b(?:float4|half4)\s+gameTime\b|\bcbuffer[^\{]*\{[\s\S]*?\bgameTime\b)",
           QRegularExpression::CaseInsensitiveOption)))
    {
        const int replacements = result.adaptedSource.count(QRegularExpression(
            R"(\bgameTime\s*\.\s*w\b)", QRegularExpression::CaseInsensitiveOption));
        if(replacements > 0)
        {
            result.adaptedSource.replace(QRegularExpression(
                R"(\bgameTime\s*\.\s*w\b)", QRegularExpression::CaseInsensitiveOption),
                "GetTime()");
            result.adaptedSourceRequired = true;
            ensurePostFxInclude(result.adaptedSource);
            result.diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_TIME_HELPER",
                "Replaced synthetic gameTime.w use with BO3 GetTime() in the adapted package copy.");
        }
    }
    if(!result.adaptedSource.contains(QRegularExpression(
           R"(\b(?:float4|half4)\s+renderTargetSize\b|\bcbuffer[^\{]*\{[\s\S]*?\brenderTargetSize\b)",
           QRegularExpression::CaseInsensitiveOption)))
    {
        const int replacements = result.adaptedSource.count(QRegularExpression(
            R"(\brenderTargetSize\b)", QRegularExpression::CaseInsensitiveOption));
        if(replacements > 0)
        {
            result.adaptedSource.replace(QRegularExpression(
                R"(\brenderTargetSize\b)", QRegularExpression::CaseInsensitiveOption),
                "PostFx_GetRenderTargetSize()");
            result.adaptedSourceRequired = true;
            ensurePostFxInclude(result.adaptedSource);
            result.diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_SIZE_HELPER",
                "Replaced synthetic renderTargetSize use with BO3 PostFx_GetRenderTargetSize() in the adapted package copy.");
        }
    }

    if(!effectiveAnalysis.hasVertexEntry)
    {
        if(!effectiveAnalysis.fullscreenInputIsAdaptable)
        {
            result.confidence = AutomationConfidence::Unsupported;
            result.diagnostics.add(DiagnosticLevel::Error, "ADAPTER_POSTFX_VS_UNSAFE",
                "No authored vs_main exists and the ps_main input is not the proven float4 SV_Position + float2 TEXCOORD0 fullscreen contract. A vertex stage cannot be generated safely.");
            return result;
        }
        ensurePostFxInclude(result.adaptedSource);
        result.adaptedSource += QString(R"(

// Generated in an adapted package copy from the proven BO3 fullscreen contract.
struct BO3AutoFullscreenVertexInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

%1 vs_main(const BO3AutoFullscreenVertexInput vertex,
           const uint instance : INSTANCE_SEMANTIC)
{
    %1 output = (%1)0;
    PostFx_GenerateFullscreenQuad(vertex.position, vertex.texcoord, instance,
                                  output.%2, output.%3);
    return output;
}
)").arg(effectiveAnalysis.pixelInputType, effectiveAnalysis.positionField, effectiveAnalysis.texcoordField);
        result.adaptedSourceRequired = true;
        result.generatedVertexStage = true;
        result.diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_VS_GENERATED",
            "Generated the proven PostFx_GenerateFullscreenQuad vertex stage in the adapted package copy.");
    }
    else
    {
        result.diagnostics.add(DiagnosticLevel::Info, "ADAPTER_POSTFX_VS_PRESERVED",
            "Preserved the shader's authored vs_main; compiled VS/PS semantic validation remains authoritative.");
    }

    result.techset.sourceName = "auto_postfx.techsetdef";
    result.techset.includes << "lit_base_shaders";
    result.techset.globals.category = "2d";
    result.techset.globals.renderFlagsText = "none";

    for(const auto& sampler : effectiveAnalysis.samplers)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = sampler.name;
        mapping.resourceKind = CompiledResourceKind::Sampler;
        mapping.choices = {"keep", "bilinearClampler", "pointClampler", "colorSampler", "ignore"};
        mapping.selectedBinding = request.mappingOverrides.value(sampler.name,
            isKnownSampler(sampler.name) ? "keep" : QString());
        if(mapping.selectedBinding.isEmpty())
        {
            mapping.confidence = AutomationConfidence::Guided;
            mapping.reason = "This sampler name is not established by the BO3 corpus. Choose a known sampler or explicitly keep an authored parameter.";
            lowerConfidence(result.confidence, mapping.confidence);
        }
        else
        {
            mapping.confidence = AutomationConfidence::Auto;
            mapping.reason = isKnownSampler(sampler.name)
                ? "The sampler is a known BO3 package parameter."
                : "The mapping was explicitly selected.";
        }

        QString parameterName = sampler.name;
        if(mapping.selectedBinding != "keep" && mapping.selectedBinding != "ignore" &&
           !mapping.selectedBinding.isEmpty())
        {
            const QRegularExpression token(QString(R"(\b%1\b)")
                .arg(QRegularExpression::escape(sampler.name)));
            result.adaptedSource.replace(token, mapping.selectedBinding);
            parameterName = mapping.selectedBinding;
            result.adaptedSourceRequired = true;
        }
        if(mapping.selectedBinding != "ignore")
        {
            ParameterModel parameter;
            parameter.kind = ParameterKind::Sampler;
            parameter.name = parameterName;
            parameter.properties["tile"] = "no tile";
            parameter.properties["filter"] = sampler.name.contains("depth", Qt::CaseInsensitive)
                ? "nearest (mip none)" : "linear (mip none)";
            result.techset.parameters << parameter;
        }
        result.mappings << mapping;
    }

    for(const auto& texture : effectiveAnalysis.textures)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = texture.name;
        mapping.resourceKind = CompiledResourceKind::Texture;
        mapping.choices = {"resolvedScene", "floatZ", "materialImage", "ignore"};
        QString reason;
        mapping.role = postFxTextureRole(texture, request.source, reason);
        mapping.selectedBinding = request.mappingOverrides.value(texture.name);
        if(!mapping.selectedBinding.isEmpty()) mapping.role = roleFromChoice(mapping.selectedBinding);
        else if(mapping.role == PackageResourceRole::ResolvedScene) mapping.selectedBinding = "resolvedScene";
        else if(mapping.role == PackageResourceRole::FloatDepth) mapping.selectedBinding = "floatZ";
        else if(mapping.role == PackageResourceRole::MaterialImage) mapping.selectedBinding = "materialImage";
        mapping.confidence = mapping.role == PackageResourceRole::Unknown
            ? AutomationConfidence::Guided : AutomationConfidence::Auto;
        mapping.reason = mapping.selectedBinding.isEmpty() ? reason : "The mapping is proven or was explicitly selected.";
        lowerConfidence(result.confidence, mapping.confidence);

        if(mapping.role != PackageResourceRole::Ignore)
        {
            ParameterModel parameter;
            parameter.kind = ParameterKind::Texture;
            parameter.name = texture.name;
            parameter.properties["image"] = QString("Image(<colorMap%1, $white_diffuse>)")
                .arg(qMax(0, texture.bindPoint), 2, 10, QChar('0'));
            parameter.properties["semantic"] = "2d";
            parameter.hasTweak = true;
            parameter.tweak.category = "Shader Textures";
            parameter.tweak.title = texture.name;
            parameter.tweak.order = QString::number(qMax(0, texture.bindPoint) + 1);
            result.techset.parameters << parameter;
        }
        result.mappings << mapping;
    }

    adaptConvertedShadertoyColorDomain(result.adaptedSource, result.mappings,
                                      result.diagnostics, result.adaptedSourceRequired);
    adaptConvertedShadertoyResolvedSceneGeometry(
        result.adaptedSource, result.mappings,
        result.diagnostics, result.adaptedSourceRequired);

    appendAutoConstantParameters(result.techset, analyzeShaderSource(result.adaptedSource),
                                 "Shader Parameters");

    TechniqueModel technique;
    technique.names = {"lit", "unlit"};
    technique.state = "replace + nocull";
    technique.stateAssigned = true;
    technique.source = result.sourceFileName;
    technique.sourceAssigned = true;
    technique.vertexShader.kind = StageKind::Vertex;
    technique.vertexShader.assignment = StageAssignmentKind::InlineShader;
    technique.vertexShader.entryPoint = "vs_main";
    technique.pixelShader.kind = StageKind::Pixel;
    technique.pixelShader.assignment = StageAssignmentKind::InlineShader;
    technique.pixelShader.entryPoint = "ps_main";
    for(const auto& mapping : result.mappings)
    {
        if(mapping.resourceKind != CompiledResourceKind::Texture) continue;
        if(mapping.role != PackageResourceRole::ResolvedScene &&
           mapping.role != PackageResourceRole::FloatDepth) continue;
        StageResourceBindingModel binding;
        binding.parameterName = mapping.resourceName;
        binding.valueKind = BindingValueKind::CodeTexture;
        binding.valueName = mapping.role == PackageResourceRole::ResolvedScene ? "resolvedScene" : "floatZ";
        binding.preprocessorCondition = "TOOLSGFX != \"1\"";
        technique.pixelShader.resourceBindings << binding;
    }
    result.techset.techniques << technique;

    if(result.confidence == AutomationConfidence::Guided)
        result.diagnostics.add(DiagnosticLevel::Warning, "ADAPTER_NEEDS_MAPPING",
            "One or more resources need an explicit BO3 binding choice before package generation.");
    return result;
}

} // namespace bo3
