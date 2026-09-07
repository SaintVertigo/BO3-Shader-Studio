#include "bo3_package_adapter.h"

#include "bo3_techset_writer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace bo3
{
namespace
{

TextureDimension textureDimension(const QString& type)
{
    if(type.compare("Texture1D", Qt::CaseInsensitive) == 0) return TextureDimension::Texture1D;
    if(type.compare("Texture2D", Qt::CaseInsensitive) == 0) return TextureDimension::Texture2D;
    if(type.compare("Texture3D", Qt::CaseInsensitive) == 0) return TextureDimension::Texture3D;
    if(type.compare("TextureCube", Qt::CaseInsensitive) == 0) return TextureDimension::TextureCube;
    return TextureDimension::Unknown;
}

QString canonicalTarget(PackageTarget target)
{
    switch(target)
    {
        case PackageTarget::PostFx: return "postfx";
        case PackageTarget::Material: return "material";
        case PackageTarget::Skybox: return "skybox";
    }
    return "postfx";
}

PackageTarget parseTarget(const QString& value)
{
    if(value.compare("material", Qt::CaseInsensitive) == 0) return PackageTarget::Material;
    if(value.compare("skybox", Qt::CaseInsensitive) == 0) return PackageTarget::Skybox;
    return PackageTarget::PostFx;
}

bool isTopLevelSourcePosition(const QString& source, int position)
{
    int depth = 0;
    bool lineComment = false;
    bool blockComment = false;
    QChar quote;
    for(int i = 0; i < position; ++i)
    {
        const QChar current = source[i];
        const QChar next = i + 1 < position ? source[i + 1] : QChar();
        if(lineComment)
        {
            if(current == '\n') lineComment = false;
            continue;
        }
        if(blockComment)
        {
            if(current == '*' && next == '/') { blockComment = false; ++i; }
            continue;
        }
        if(!quote.isNull())
        {
            if(current == '\\') { ++i; continue; }
            if(current == quote) quote = QChar();
            continue;
        }
        if(current == '/' && next == '/') { lineComment = true; ++i; continue; }
        if(current == '/' && next == '*') { blockComment = true; ++i; continue; }
        if(current == '"' || current == '\'') { quote = current; continue; }
        if(current == '{') ++depth;
        else if(current == '}') depth = qMax(0, depth - 1);
    }
    return depth == 0 && !lineComment && !blockComment && quote.isNull();
}

ParameterKind constantKind(const QString& type)
{
    const QString lower = type.toLower();
    if(lower == "float" || lower == "half") return ParameterKind::Float1;
    if(lower == "float2" || lower == "half2") return ParameterKind::Float2;
    if(lower == "float3" || lower == "half3") return ParameterKind::Float3;
    if(lower == "float4" || lower == "half4") return ParameterKind::Float4;
    if(lower == "uint") return ParameterKind::UInt1;
    if(lower == "uint2") return ParameterKind::UInt2;
    if(lower == "uint3") return ParameterKind::UInt3;
    if(lower == "uint4") return ParameterKind::UInt4;
    if(lower == "bool") return ParameterKind::Bool;
    return ParameterKind::Unknown;
}

int constantComponentCount(ParameterKind kind)
{
    switch(kind)
    {
        case ParameterKind::Float2:
        case ParameterKind::UInt2: return 2;
        case ParameterKind::Float3:
        case ParameterKind::UInt3: return 3;
        case ParameterKind::Float4:
        case ParameterKind::UInt4: return 4;
        case ParameterKind::Float1:
        case ParameterKind::UInt1:
        case ParameterKind::Bool: return 1;
        default: return 0;
    }
}

} // namespace

ShaderSourceAnalysis analyzeShaderSource(const QString& source)
{
    ShaderSourceAnalysis analysis;
    analysis.source = source;

    QSet<QString> resourceNames;
    const QRegularExpression textureRe(
        R"(\b(Texture1D|Texture2D|Texture3D|TextureCube)(?:\s*<[^>]+>)?\s+([A-Za-z_]\w*)\s*(?::\s*register\s*\(\s*t(\d+)\s*\))?\s*;)",
        QRegularExpression::CaseInsensitiveOption);
    auto textureIt = textureRe.globalMatch(source);
    int nextTextureSlot = 0;
    while(textureIt.hasNext())
    {
        const auto match = textureIt.next();
        // Resource regexes intentionally scan the raw source so register slots
        // remain easy to recover, but declarations inside comments or nested
        // scopes must never become package parameters. A commented-out
        // frameBuffer previously survived this analysis and caused the Runtime
        // auto-techset to bind a parameter that FXC had correctly removed.
        if(!isTopLevelSourcePosition(source, match.capturedStart())) continue;
        if(resourceNames.contains(match.captured(2))) continue;
        resourceNames.insert(match.captured(2));
        const int slot = match.captured(3).isEmpty() ? nextTextureSlot : match.captured(3).toInt();
        nextTextureSlot = qMax(nextTextureSlot, slot + 1);
        analysis.textures.push_back({match.captured(2), CompiledResourceKind::Texture,
                                     textureDimension(match.captured(1)), slot});
    }

    const QRegularExpression samplerRe(
        R"(\b(SamplerState|SamplerComparisonState)\s+([A-Za-z_]\w*)\s*(?::\s*register\s*\(\s*s(\d+)\s*\))?\s*;)",
        QRegularExpression::CaseInsensitiveOption);
    auto samplerIt = samplerRe.globalMatch(source);
    int nextSamplerSlot = 0;
    while(samplerIt.hasNext())
    {
        const auto match = samplerIt.next();
        if(!isTopLevelSourcePosition(source, match.capturedStart())) continue;
        if(resourceNames.contains(match.captured(2))) continue;
        resourceNames.insert(match.captured(2));
        const int slot = match.captured(3).isEmpty() ? nextSamplerSlot : match.captured(3).toInt();
        nextSamplerSlot = qMax(nextSamplerSlot, slot + 1);
        analysis.samplers.push_back({match.captured(2), CompiledResourceKind::Sampler,
                                     TextureDimension::Unknown, slot});
    }

    const QRegularExpression constantRe(
        R"(^[ \t]*(float(?:[234])?|half(?:[234])?|uint(?:[234])?|bool)[ \t]+([A-Za-z_]\w*)[ \t]*(?:=[ \t]*([^;\r\n]+))?[ \t]*;)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    auto constantIt = constantRe.globalMatch(source);
    while(constantIt.hasNext())
    {
        const auto match = constantIt.next();
        if(!isTopLevelSourcePosition(source, match.capturedStart())) continue;
        analysis.constants.push_back({match.captured(2), match.captured(1),
                                      match.captured(3).trimmed()});
    }

    // Accept the real BO3/custom-material pixel signatures we generate, not
    // only a single unqualified struct parameter. In particular the generated
    // geometry wrapper uses:
    //   ps_main(const BO3CustomMaterialPixelInput pixel,
    //           const uint isFrontFace : SV_IsFrontFace)
    // The old regex required the closing ')' immediately after the first
    // optional type/name pair, so that valid wrapper was reported as if it had
    // no ps_main at all (ADAPTER_MATERIAL_NO_PS). It also missed direct HLSL
    // parameters that carry a semantic, e.g. float4 position : SV_Position.
    const QRegularExpression pixelEntryRe(
        R"(\b[A-Za-z_]\w*(?:\s*<[^>]+>)?\s+ps_main\s*\(([^)]*)\))",
        QRegularExpression::CaseInsensitiveOption);
    auto pixelEntryIt = pixelEntryRe.globalMatch(source);
    while(pixelEntryIt.hasNext())
    {
        const auto pixelEntry = pixelEntryIt.next();
        if(!isTopLevelSourcePosition(source, pixelEntry.capturedStart())) continue;

        analysis.hasPixelEntry = true;
        const QString parameters = pixelEntry.captured(1).trimmed();
        if(!parameters.isEmpty())
        {
            // BO3 entry-point parameters do not use comma-containing template
            // expressions here; the first comma therefore safely separates the
            // primary pixel-input value from auxiliary semantics such as
            // SV_IsFrontFace. Only the first parameter determines the input
            // struct contract used by the package adapters.
            const QString firstParameter = parameters.section(',', 0, 0).trimmed();
            const QRegularExpression firstParameterRe(
                R"(^\s*(?:const\s+)?([A-Za-z_]\w*(?:\s*<[^>]+>)?)\s*(?:[A-Za-z_]\w*)?(?:\s*:\s*[A-Za-z_]\w*)?\s*$)",
                QRegularExpression::CaseInsensitiveOption);
            const auto firstParameterMatch = firstParameterRe.match(firstParameter);
            if(firstParameterMatch.hasMatch())
                analysis.pixelInputType = firstParameterMatch.captured(1).trimmed();
        }
        break;
    }
    analysis.hasVertexEntry = source.contains(QRegularExpression(
        R"(\bvs_main\s*\()", QRegularExpression::CaseInsensitiveOption));
    analysis.hasBo3FullscreenVertex = analysis.hasVertexEntry && source.contains(QRegularExpression(
        R"(\bPostFx_GenerateFullscreenQuad\s*\()", QRegularExpression::CaseInsensitiveOption));
    analysis.hasPostFxInclude = source.contains(QRegularExpression(
        R"(#\s*include\s*[<\"]postfx/postfx_common\.h[>\"])", QRegularExpression::CaseInsensitiveOption));

    if(!analysis.pixelInputType.isEmpty())
    {
        const QRegularExpression structRe(
            QString(R"(\bstruct\s+%1\s*\{([\s\S]*?)\};)")
                .arg(QRegularExpression::escape(analysis.pixelInputType)),
            QRegularExpression::CaseInsensitiveOption);
        const auto structMatch = structRe.match(source);
        if(structMatch.hasMatch())
        {
            const QString body = structMatch.captured(1);
            const QRegularExpression positionRe(
                R"(\bfloat4\s+([A-Za-z_]\w*)\s*:\s*SV_?POSITION\s*;)",
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpression texcoordRe(
                R"(\bfloat2\s+([A-Za-z_]\w*)\s*:\s*TEXCOORD0?\s*;)",
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpression directionRe(
                R"(\b(float3|float4)\s+(skyDirection|viewDirection|rayDirection|viewDirWorld|worldDirection)\s*:\s*TEXCOORD\d*\s*;)",
                QRegularExpression::CaseInsensitiveOption);
            const auto position = positionRe.match(body);
            const auto texcoord = texcoordRe.match(body);
            const auto direction = directionRe.match(body);
            if(position.hasMatch()) analysis.positionField = position.captured(1);
            if(texcoord.hasMatch()) analysis.texcoordField = texcoord.captured(1);
            if(direction.hasMatch())
            {
                analysis.directionType = direction.captured(1);
                analysis.directionField = direction.captured(2);
                analysis.hasDirectionalInput = true;
            }
            analysis.fullscreenInputIsAdaptable = position.hasMatch() && texcoord.hasMatch();
        }
    }
    return analysis;
}

QString shaderInterfaceFingerprint(const ShaderSourceAnalysis& analysis, PackageTarget target)
{
    QStringList lines;
    lines << canonicalTarget(target)
          << QString("ps:%1:%2").arg(analysis.hasPixelEntry).arg(analysis.pixelInputType)
          << QString("vs:%1:%2").arg(analysis.hasVertexEntry).arg(analysis.hasBo3FullscreenVertex)
          << QString("sem:%1:%2:%3:%4").arg(
                 analysis.positionField, analysis.texcoordField,
                 analysis.directionField, analysis.directionType);
    for(const auto& texture : analysis.textures)
        lines << QString("t:%1:%2:%3").arg(texture.name).arg(static_cast<int>(texture.dimension)).arg(texture.bindPoint);
    for(const auto& sampler : analysis.samplers)
        lines << QString("s:%1:%2").arg(sampler.name).arg(sampler.bindPoint);
    for(const auto& constant : analysis.constants)
        lines << QString("c:%1:%2").arg(constant.name, constant.hlslType.toLower());
    std::sort(lines.begin(), lines.end());
    return QString::fromLatin1(QCryptographicHash::hash(lines.join('\n').toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

void appendAutoConstantParameters(
    TechsetModel& techset,
    const ShaderSourceAnalysis& analysis,
    const QString& tweakCategory,
    const QMap<QString, QString>& storageOverrides)
{
    int cgIndex = 0;
    int checkboxIndex = 0;
    static const QStringList components{"x", "y", "z", "w"};
    for(const SourceConstant& constant : analysis.constants)
    {
        if(techset.findParameter(constant.name)) continue;
        const ParameterKind kind = constantKind(constant.hlslType);
        const int componentCount = constantComponentCount(kind);
        if(kind == ParameterKind::Unknown || componentCount == 0) continue;

        ParameterModel parameter;
        parameter.kind = kind;
        parameter.name = constant.name;
        const QString overrideStorage = storageOverrides.value(constant.name);
        if(kind == ParameterKind::Bool)
        {
            parameter.properties["value"] = overrideStorage.isEmpty()
                ? QString("<gCheckBox%1>").arg(checkboxIndex++, 2, 10, QChar('0'))
                : overrideStorage;
        }
        else if(!overrideStorage.isEmpty())
        {
            // The property key is the parameter component being populated; the
            // component inside <cgNN_w> is the storage source. A float1 always
            // exposes x even when its backing slot is cg31_w.
            parameter.properties["x"] = overrideStorage;
        }
        else
        {
            for(int component = 0; component < componentCount; ++component)
                parameter.properties[components[component]] = QString("<cg%1_%2>")
                    .arg(cgIndex, 2, 10, QChar('0')).arg(components[component]);
            ++cgIndex;
        }

        const bool internalAnchor = constant.name.startsWith("bo3ExportDefaultCBufferAnchor");
        parameter.hasTweak = !internalAnchor;
        if(parameter.hasTweak)
        {
            parameter.tweak.category = tweakCategory;
            parameter.tweak.title = constant.name;
            parameter.tweak.order = QString::number(100 + techset.parameters.size());
            if(!constant.defaultValue.isEmpty())
                parameter.tweak.properties["default"] = constant.defaultValue;
        }
        techset.parameters << parameter;
    }
}

PackageAdapterResult adaptShaderPackage(const PackageAdapterRequest& request)
{
    const ShaderSourceAnalysis analysis = analyzeShaderSource(request.source);
    PackageAdapterResult result;
    switch(request.target)
    {
        case PackageTarget::PostFx: result = adaptPostFxPackage(request, analysis); break;
        case PackageTarget::Material: result = adaptMaterialPackage(request, analysis); break;
        case PackageTarget::Skybox: result = adaptSkyPackage(request, analysis); break;
    }
    result.target = request.target;
    result.interfaceFingerprint = shaderInterfaceFingerprint(
        analyzeShaderSource(result.adaptedSource.isEmpty() ? request.source : result.adaptedSource),
        request.target);
    return result;
}

QString serializeAutoTechset(const PackageAdapterResult& result)
{
    return QString("// BO3HLSLPreviewer-AutoTechset: 1\n"
                   "// Target: %1\n"
                   "// Interface-Fingerprint: %2\n"
                   "// Regeneration: review required when this fingerprint changes.\n\n%3")
        .arg(canonicalTarget(result.target), result.interfaceFingerprint,
             serializeTechset(result.techset));
}

AutoTechsetMetadata readAutoTechsetMetadata(const QString& techsetSource, bool explicitlyImported)
{
    AutoTechsetMetadata metadata;
    if(techsetSource.trimmed().isEmpty()) return metadata;
    if(explicitlyImported)
    {
        metadata.origin = TechsetOrigin::Imported;
        metadata.valid = true;
        return metadata;
    }
    const QRegularExpression markerRe(
        R"(^\s*//\s*BO3HLSLPreviewer-AutoTechset\s*:\s*1\s*$)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if(!techsetSource.contains(markerRe))
    {
        metadata.origin = TechsetOrigin::Authored;
        metadata.valid = true;
        return metadata;
    }
    metadata.origin = TechsetOrigin::AutoGenerated;
    const auto target = QRegularExpression(
        R"(^\s*//\s*Target\s*:\s*([A-Za-z]+)\s*$)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption).match(techsetSource);
    const auto fingerprint = QRegularExpression(
        R"(^\s*//\s*Interface-Fingerprint\s*:\s*([0-9a-f]{64})\s*$)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption).match(techsetSource);
    metadata.target = parseTarget(target.captured(1));
    metadata.interfaceFingerprint = fingerprint.captured(1).toLower();
    metadata.valid = target.hasMatch() && fingerprint.hasMatch();
    return metadata;
}

bool isAutoTechsetStale(const QString& techsetSource, const QString& currentFingerprint)
{
    const AutoTechsetMetadata metadata = readAutoTechsetMetadata(techsetSource);
    return metadata.origin == TechsetOrigin::AutoGenerated &&
           (!metadata.valid || metadata.interfaceFingerprint.compare(
                currentFingerprint, Qt::CaseInsensitive) != 0);
}

QString adjacentTechsetPath(const QString& shaderPath)
{
    const QFileInfo shader(shaderPath);
    if(!shader.exists() || !shader.isFile()) return {};
    const QString candidate = shader.dir().filePath(shader.completeBaseName() + ".techsetdef");
    const QFileInfo techset(candidate);
    return techset.exists() && techset.isFile() ? techset.absoluteFilePath() : QString();
}

QString toString(PackageTarget target)
{
    switch(target)
    {
        case PackageTarget::PostFx: return "BO3 PostFX";
        case PackageTarget::Material: return "BO3 Material";
        case PackageTarget::Skybox: return "BO3 Skybox";
    }
    return "BO3 PostFX";
}

QString toString(AutomationConfidence confidence)
{
    switch(confidence)
    {
        case AutomationConfidence::Auto: return "AUTO";
        case AutomationConfidence::Guided: return "GUIDED";
        case AutomationConfidence::Unsupported: return "UNSUPPORTED";
    }
    return "UNSUPPORTED";
}

QString toString(TechsetOrigin origin)
{
    switch(origin)
    {
        case TechsetOrigin::None: return "None";
        case TechsetOrigin::Authored: return "Authored";
        case TechsetOrigin::AutoGenerated: return "Auto-generated";
        case TechsetOrigin::Imported: return "Imported";
    }
    return "None";
}

QString toString(PackageResourceRole role)
{
    switch(role)
    {
        case PackageResourceRole::Unknown: return "Unknown";
        case PackageResourceRole::ResolvedScene: return "resolvedScene";
        case PackageResourceRole::FloatDepth: return "floatZ";
        case PackageResourceRole::MaterialImage: return "Material image";
        case PackageResourceRole::Color: return "Color / albedo";
        case PackageResourceRole::Normal: return "Normal";
        case PackageResourceRole::Opacity: return "Opacity";
        case PackageResourceRole::Emissive: return "Emissive";
        case PackageResourceRole::Mask: return "Mask";
        case PackageResourceRole::CubeMap: return "Cubemap";
        case PackageResourceRole::Ignore: return "Ignore";
    }
    return "Unknown";
}

} // namespace bo3
