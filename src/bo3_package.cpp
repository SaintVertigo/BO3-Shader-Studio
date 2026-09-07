#include "bo3_package.h"

#include <QRegularExpression>

namespace bo3
{

void ValidationResult::add(DiagnosticLevel level, const QString& code, const QString& message,
                           const SourceLocation& location)
{
    diagnostics.push_back({level, code, message, location});
}

void ValidationResult::append(const ValidationResult& other)
{
    diagnostics += other.diagnostics;
}

bool ValidationResult::hasErrors() const
{
    for(const Diagnostic& diagnostic : diagnostics)
        if(diagnostic.level == DiagnosticLevel::Error) return true;
    return false;
}

bool ValidationResult::hasWarnings() const
{
    for(const Diagnostic& diagnostic : diagnostics)
        if(diagnostic.level == DiagnosticLevel::Warning) return true;
    return false;
}

bool ValidationResult::hasUnknowns() const
{
    for(const Diagnostic& diagnostic : diagnostics)
        if(diagnostic.level == DiagnosticLevel::Unknown) return true;
    return false;
}

CompatibilityStatus ValidationResult::status() const
{
    if(hasErrors()) return CompatibilityStatus::Fail;
    if(hasUnknowns()) return CompatibilityStatus::Unknown;
    if(hasWarnings()) return CompatibilityStatus::Warning;
    return CompatibilityStatus::Pass;
}

QString ValidationResult::toText() const
{
    QStringList lines;
    for(const Diagnostic& diagnostic : diagnostics)
    {
        QString where;
        if(!diagnostic.location.sourceName.isEmpty())
        {
            where = diagnostic.location.sourceName;
            if(diagnostic.location.line > 0) where += ":" + QString::number(diagnostic.location.line);
            where += ": ";
        }
        lines << QString("[%1] %2%3: %4")
                     .arg(toString(diagnostic.level), where, diagnostic.code, diagnostic.message);
    }
    return lines.join('\n');
}

const ParameterModel* TechsetModel::findParameter(const QString& name) const
{
    for(auto it = parameters.crbegin(); it != parameters.crend(); ++it)
        if(it->name.compare(name, Qt::CaseSensitive) == 0) return &*it;
    return nullptr;
}

const TechniqueModel* TechsetModel::findTechnique(const QString& name) const
{
    for(auto it = techniques.crbegin(); it != techniques.crend(); ++it)
        for(const QString& candidate : it->names)
            if(candidate.compare(name, Qt::CaseSensitive) == 0) return &*it;
    return nullptr;
}

const CompiledResource* CompiledShaderInterface::findResource(const QString& name) const
{
    for(const CompiledResource& resource : resources)
        if(resource.name.compare(name, Qt::CaseSensitive) == 0) return &resource;
    return nullptr;
}

QString toString(PackageConfiguration value)
{
    return value == PackageConfiguration::Toolsgfx ? "TOOLSGFX" : "Runtime";
}

QString toString(CompatibilityStatus value)
{
    switch(value)
    {
        case CompatibilityStatus::Pass: return "PASS";
        case CompatibilityStatus::Warning: return "WARNING";
        case CompatibilityStatus::Fail: return "FAIL";
        case CompatibilityStatus::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

QString toString(DiagnosticLevel value)
{
    switch(value)
    {
        case DiagnosticLevel::Info: return "INFO";
        case DiagnosticLevel::Warning: return "WARNING";
        case DiagnosticLevel::Error: return "ERROR";
        case DiagnosticLevel::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

QString toString(ParameterKind value)
{
    switch(value)
    {
        case ParameterKind::Sampler: return "Sampler";
        case ParameterKind::Texture: return "Texture";
        case ParameterKind::Float1: return "float1";
        case ParameterKind::Float2: return "float2";
        case ParameterKind::Float3: return "float3";
        case ParameterKind::Float4: return "float4";
        case ParameterKind::UInt1: return "uint1";
        case ParameterKind::UInt2: return "uint2";
        case ParameterKind::UInt3: return "uint3";
        case ParameterKind::UInt4: return "uint4";
        case ParameterKind::Bool: return "Bool";
        case ParameterKind::Color: return "Color";
        case ParameterKind::Unknown: break;
    }
    return "Unknown";
}

QString toString(StageKind value)
{
    switch(value)
    {
        case StageKind::Vertex: return "VS";
        case StageKind::Pixel: return "PS";
        case StageKind::Geometry: return "GS";
        case StageKind::Hull: return "HS";
        case StageKind::Domain: return "DS";
        case StageKind::Compute: return "CS";
    }
    return "Unknown";
}

QString toString(CompiledResourceKind value)
{
    switch(value)
    {
        case CompiledResourceKind::Texture: return "Texture";
        case CompiledResourceKind::Sampler: return "Sampler";
        case CompiledResourceKind::ConstantBuffer: return "ConstantBuffer";
        case CompiledResourceKind::StructuredBuffer: return "StructuredBuffer";
        case CompiledResourceKind::ByteAddressBuffer: return "ByteAddressBuffer";
        case CompiledResourceKind::UnorderedAccess: return "UnorderedAccess";
        case CompiledResourceKind::Unknown: break;
    }
    return "Unknown";
}

QString toString(TextureDimension value)
{
    switch(value)
    {
        case TextureDimension::Buffer: return "Buffer";
        case TextureDimension::Texture1D: return "Texture1D";
        case TextureDimension::Texture1DArray: return "Texture1DArray";
        case TextureDimension::Texture2D: return "Texture2D";
        case TextureDimension::Texture2DArray: return "Texture2DArray";
        case TextureDimension::Texture2DMS: return "Texture2DMS";
        case TextureDimension::Texture2DMSArray: return "Texture2DMSArray";
        case TextureDimension::Texture3D: return "Texture3D";
        case TextureDimension::TextureCube: return "TextureCube";
        case TextureDimension::TextureCubeArray: return "TextureCubeArray";
        case TextureDimension::Unknown: break;
    }
    return "Unknown";
}

RenderStateModel interpretRenderState(const QString& state)
{
    RenderStateModel result;
    result.raw = state.trimmed();
    const QString normalized = result.raw.toLower();
    result.tokens = normalized.split(QRegularExpression("\\s*\\+\\s*|\\s+"), Qt::SkipEmptyParts);

    const QSet<QString> known = {
        "replace", "blend", "add", "gbuffer", "fallback", "depth", "nocull",
        "decal", "opaque", "depthprepass", "rez", "sky"
    };
    for(const QString& token : result.tokens)
        if(!known.contains(token)) result.unknownTokens << token;

    if(result.tokens.contains("replace")) result.blend = RenderBlendMode::Replace;
    else if(result.tokens.contains("blend")) result.blend = RenderBlendMode::Blend;
    else if(result.tokens.contains("add")) result.blend = RenderBlendMode::Add;
    else if(result.tokens.contains("gbuffer")) result.blend = RenderBlendMode::GBuffer;
    else if(result.tokens.contains("fallback")) result.blend = RenderBlendMode::Fallback;

    result.depth = result.tokens.contains("depth");
    result.noCull = result.tokens.contains("nocull");
    result.decal = result.tokens.contains("decal");
    result.opaque = result.tokens.contains("opaque");
    result.depthPrepass = result.tokens.contains("depthprepass");
    result.rez = result.tokens.contains("rez");
    result.sky = result.tokens.contains("sky");
    return result;
}

} // namespace bo3
