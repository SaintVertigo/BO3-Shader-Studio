#include "bo3_package_adapter.h"

#include <QFileInfo>
#include <QSet>

namespace bo3
{
namespace
{

void lowerConfidence(AutomationConfidence& current, AutomationConfidence value)
{
    if(static_cast<int>(value) > static_cast<int>(current)) current = value;
}

PackageResourceRole classifyMaterialTexture(const QString& name)
{
    const QString lower = name.toLower();
    if(lower.contains("normal")) return PackageResourceRole::Normal;
    if(lower.contains("opacity") || lower.contains("alpha") || lower.contains("reveal")) return PackageResourceRole::Opacity;
    if(lower.contains("emiss") || lower.contains("glow")) return PackageResourceRole::Emissive;
    if(lower.contains("mask")) return PackageResourceRole::Mask;
    if(lower.contains("color") || lower.contains("albedo") || lower.contains("diffuse")) return PackageResourceRole::Color;
    return PackageResourceRole::Unknown;
}

PackageResourceRole roleFromChoice(const QString& choice)
{
    if(choice.compare("color", Qt::CaseInsensitive) == 0) return PackageResourceRole::Color;
    if(choice.compare("normal", Qt::CaseInsensitive) == 0) return PackageResourceRole::Normal;
    if(choice.compare("opacity", Qt::CaseInsensitive) == 0) return PackageResourceRole::Opacity;
    if(choice.compare("emissive", Qt::CaseInsensitive) == 0) return PackageResourceRole::Emissive;
    if(choice.compare("mask", Qt::CaseInsensitive) == 0) return PackageResourceRole::Mask;
    if(choice.compare("materialImage", Qt::CaseInsensitive) == 0) return PackageResourceRole::MaterialImage;
    if(choice.compare("ignore", Qt::CaseInsensitive) == 0) return PackageResourceRole::Ignore;
    return PackageResourceRole::Unknown;
}

QString imagePlaceholder(PackageResourceRole role, int slot)
{
    switch(role)
    {
        case PackageResourceRole::Color: return "colorMap";
        case PackageResourceRole::Normal: return "normalMap";
        case PackageResourceRole::Opacity: return "alphaRevealMap";
        case PackageResourceRole::Emissive: return "colorMap01";
        case PackageResourceRole::Mask: return "colorMap02";
        default: return QString("colorMap%1").arg(qMax(0, slot), 2, 10, QChar('0'));
    }
}

QString imageFallback(PackageResourceRole role)
{
    if(role == PackageResourceRole::Normal) return "$identitynormalmap";
    if(role == PackageResourceRole::Opacity) return "$white_reveal";
    return "$white_diffuse";
}

QString imageSemantic(PackageResourceRole role)
{
    switch(role)
    {
        case PackageResourceRole::Color: return "diffuseMap";
        case PackageResourceRole::Normal: return "normalMap";
        case PackageResourceRole::Opacity: return "revealMap";
        case PackageResourceRole::Emissive: return "emissiveMap";
        case PackageResourceRole::Mask: return "2d";
        default: return "2d";
    }
}

} // namespace

PackageAdapterResult adaptMaterialPackage(const PackageAdapterRequest& request,
                                          const ShaderSourceAnalysis& analysis)
{
    PackageAdapterResult result;
    result.target = PackageTarget::Material;
    result.confidence = AutomationConfidence::Auto;
    result.sourceFileName = QFileInfo(request.sourceFileName).fileName();
    if(result.sourceFileName.isEmpty()) result.sourceFileName = "shader_bo3_material.hlsl";
    result.adaptedSource = request.source;

    if(!analysis.hasPixelEntry)
    {
        result.confidence = AutomationConfidence::Unsupported;
        result.diagnostics.add(DiagnosticLevel::Error, "ADAPTER_MATERIAL_NO_PS",
            "Material packaging requires a ps_main pixel entry point or the existing BO3 material adapter output.");
        return result;
    }

    const bool deferred = request.source.contains("GBufferPixelOutput") ||
                          request.source.contains("BO3_PREVIEWER_MATERIAL_SURFACE: OPAQUE");
    result.selectedTechnique = deferred ? "gbuffer" : "lit";
    result.techset.sourceName = "auto_material.techsetdef";
    // Preview/package adaptation follows the same self-contained custom-material
    // contract as export. Textureless procedural shaders must not inherit the
    // stock deferred colorMap requirement from lit_base_mid.
    result.techset.includes << "lit_base_shaders";
    if(deferred) result.techset.includes << "shadowmap_technique_base";
    result.techset.globals.category = "Geometry Custom";
    result.techset.globals.availablePrefixes = "mc/ mcs/ wc/";
    result.techset.globals.renderFlagsText = deferred ? "lit deferred opaque" : "emissive";

    for(const auto& sampler : analysis.samplers)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = sampler.name;
        mapping.resourceKind = CompiledResourceKind::Sampler;
        mapping.selectedBinding = "keep";
        mapping.choices = {"keep", "colorSampler", "normalSampler", "ignore"};
        mapping.confidence = AutomationConfidence::Auto;
        mapping.reason = "Material samplers are exposed explicitly by the generated techset parameter.";
        if(request.mappingOverrides.value(sampler.name) == "ignore")
            mapping.selectedBinding = "ignore";
        if(mapping.selectedBinding != "ignore")
        {
            ParameterModel parameter;
            parameter.kind = ParameterKind::Sampler;
            parameter.name = sampler.name;
            parameter.properties["tile"] = "tile both";
            parameter.properties["filter"] = "linear (mip linear)";
            result.techset.parameters << parameter;
        }
        result.mappings << mapping;
    }

    for(const auto& texture : analysis.textures)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = texture.name;
        mapping.resourceKind = CompiledResourceKind::Texture;
        mapping.choices = {"color", "normal", "opacity", "emissive", "mask", "materialImage", "ignore"};
        mapping.role = classifyMaterialTexture(texture.name);
        mapping.selectedBinding = request.mappingOverrides.value(texture.name);
        if(!mapping.selectedBinding.isEmpty()) mapping.role = roleFromChoice(mapping.selectedBinding);
        else
        {
            switch(mapping.role)
            {
                case PackageResourceRole::Color: mapping.selectedBinding = "color"; break;
                case PackageResourceRole::Normal: mapping.selectedBinding = "normal"; break;
                case PackageResourceRole::Opacity: mapping.selectedBinding = "opacity"; break;
                case PackageResourceRole::Emissive: mapping.selectedBinding = "emissive"; break;
                case PackageResourceRole::Mask: mapping.selectedBinding = "mask"; break;
                default: break;
            }
        }
        mapping.confidence = mapping.role == PackageResourceRole::Unknown
            ? AutomationConfidence::Guided : AutomationConfidence::Auto;
        mapping.reason = mapping.confidence == AutomationConfidence::Auto
            ? "The resource name provides a strong material semantic and an APE-visible image parameter can be generated."
            : "The texture semantic is ambiguous; choose its APE material role.";
        lowerConfidence(result.confidence, mapping.confidence);

        if(mapping.role != PackageResourceRole::Ignore)
        {
            ParameterModel parameter;
            parameter.kind = ParameterKind::Texture;
            parameter.name = texture.name;
            const QString placeholder = imagePlaceholder(mapping.role, texture.bindPoint);
            parameter.properties["image"] = QString("Image(<%1, %2>)")
                .arg(placeholder, imageFallback(mapping.role));
            parameter.properties["semantic"] = imageSemantic(mapping.role);
            parameter.hasTweak = true;
            parameter.tweak.category = "Material Images";
            parameter.tweak.title = texture.name;
            parameter.tweak.order = QString::number(qMax(0, texture.bindPoint) * 5);
            result.techset.parameters << parameter;
        }
        result.mappings << mapping;
    }

    appendAutoConstantParameters(result.techset, analysis, "Custom HLSL",
        {{"bo3MaterialOutputScale", "<cg31_w>"},
         {"bo3MaterialOpacity", "<cg31_z>"},
         {"bo3MaterialAlphaCutoff", "<cg31_y>"}});

    TechniqueModel technique;
    technique.names = deferred ? QStringList{"gbuffer"} : QStringList{"lit", "unlit"};
    technique.state = deferred ? "gbuffer opaque" : "replace + depth + nocull";
    technique.stateAssigned = true;
    technique.source = result.sourceFileName;
    technique.sourceAssigned = true;
    technique.vertexShader.kind = StageKind::Vertex;
    technique.vertexShader.assignment = analysis.hasVertexEntry
        ? StageAssignmentKind::InlineShader : StageAssignmentKind::GenericName;
    technique.vertexShader.genericName = analysis.hasVertexEntry ? QString() : "vs_generic";
    technique.vertexShader.entryPoint = analysis.hasVertexEntry ? "vs_main" : QString();
    technique.pixelShader.kind = StageKind::Pixel;
    technique.pixelShader.assignment = StageAssignmentKind::InlineShader;
    technique.pixelShader.entryPoint = "ps_main";
    result.techset.techniques << technique;

    if(deferred)
    {
        TechniqueModel motion;
        motion.names = {"gbuffer motion vector"};
        motion.baseName = "gbuffer";
        motion.vertexShader.kind = StageKind::Vertex;
        motion.pixelShader.kind = StageKind::Pixel;
        motion.definesAppend = {"GENERATE_MOTION_VECTOR"};
        result.techset.techniques << motion;
    }

    if(result.confidence == AutomationConfidence::Guided)
        result.diagnostics.add(DiagnosticLevel::Warning, "ADAPTER_NEEDS_MAPPING",
            "One or more material images need an explicit APE role before package generation.");
    return result;
}

} // namespace bo3
