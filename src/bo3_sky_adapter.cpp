#include "bo3_package_adapter.h"

#include <QFileInfo>

namespace bo3
{

PackageAdapterResult adaptSkyPackage(const PackageAdapterRequest& request,
                                     const ShaderSourceAnalysis& analysis)
{
    PackageAdapterResult result;
    result.target = PackageTarget::Skybox;
    result.confidence = AutomationConfidence::Auto;
    result.sourceFileName = QFileInfo(request.sourceFileName).fileName();
    if(result.sourceFileName.isEmpty()) result.sourceFileName = "shader_bo3_skybox.hlsl";
    result.adaptedSource = request.source;

    if(!analysis.hasPixelEntry)
    {
        result.confidence = AutomationConfidence::Unsupported;
        result.diagnostics.add(DiagnosticLevel::Error, "ADAPTER_SKY_NO_PS",
            "Skybox packaging requires a ps_main pixel entry point.");
        return result;
    }
    if(!analysis.hasDirectionalInput)
    {
        result.confidence = AutomationConfidence::Unsupported;
        result.diagnostics.add(DiagnosticLevel::Error, "ADAPTER_SKY_COORDINATE_MODEL",
            "The shader exposes only a fullscreen/image coordinate model. Converting it to a sky direction would change its semantics and requires guided source work.");
        return result;
    }

    result.techset.sourceName = "auto_skybox.techsetdef";
    result.techset.includes << "sky_base";
    result.techset.globals.category = "Geometry";
    result.techset.globals.renderFlagsText = "emissive sky";
    result.techset.globals.availablePrefixes = "mc/ mcs/ wc/";

    for(const auto& sampler : analysis.samplers)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = sampler.name;
        mapping.resourceKind = CompiledResourceKind::Sampler;
        mapping.selectedBinding = "keep";
        mapping.choices = {"keep", "colorSampler", "ignore"};
        mapping.confidence = AutomationConfidence::Auto;
        mapping.reason = "The sampler is represented explicitly in the sky techset.";
        ParameterModel parameter;
        parameter.kind = ParameterKind::Sampler;
        parameter.name = sampler.name;
        parameter.properties["tile"] = "tile both";
        parameter.properties["filter"] = "aniso4x (mip linear)";
        result.techset.parameters << parameter;
        result.mappings << mapping;
    }

    for(const auto& texture : analysis.textures)
    {
        PackageResourceMapping mapping;
        mapping.resourceName = texture.name;
        mapping.resourceKind = CompiledResourceKind::Texture;
        mapping.choices = {"cubemap", "materialImage", "ignore"};
        const QString override = request.mappingOverrides.value(texture.name);
        const bool knownCube = texture.dimension == TextureDimension::TextureCube ||
                               texture.name.compare("colorMap", Qt::CaseInsensitive) == 0;
        mapping.role = override == "ignore" ? PackageResourceRole::Ignore :
                       (knownCube || override == "cubemap" ? PackageResourceRole::CubeMap :
                        (override == "materialImage" ? PackageResourceRole::MaterialImage : PackageResourceRole::Unknown));
        mapping.selectedBinding = !override.isEmpty() ? override : (knownCube ? "cubemap" : QString());
        mapping.confidence = mapping.role == PackageResourceRole::Unknown
            ? AutomationConfidence::Guided : AutomationConfidence::Auto;
        mapping.reason = knownCube
            ? "The reflected cubemap/colorMap is a proven BO3 sky material image."
            : "Choose whether this texture is a sky material image or cubemap.";
        if(mapping.confidence == AutomationConfidence::Guided)
            result.confidence = AutomationConfidence::Guided;

        if(mapping.role != PackageResourceRole::Ignore)
        {
            ParameterModel parameter;
            parameter.kind = ParameterKind::Texture;
            parameter.name = texture.name;
            parameter.properties["ref"] = "true";
            parameter.properties["image"] = QString("Image(<%1, $white_diffuse_cube_hdr>)")
                .arg(texture.name.compare("colorMap", Qt::CaseInsensitive) == 0 ? "colorMap" : texture.name);
            parameter.properties["semantic"] = "HDR";
            parameter.hasTweak = true;
            parameter.tweak.category = "Color";
            parameter.tweak.title = texture.name;
            parameter.tweak.order = QString::number(qMax(0, texture.bindPoint) * 5);
            result.techset.parameters << parameter;
        }
        result.mappings << mapping;
    }

    appendAutoConstantParameters(result.techset, analysis, "Sky Parameters");

    TechniqueModel lit;
    lit.names = {"lit"};
    lit.state = "sky";
    lit.stateAssigned = true;
    lit.source = analysis.hasVertexEntry ? result.sourceFileName
                                         : "techsetdef_sky_latlong_hdr.hlsl";
    lit.sourceAssigned = true;
    lit.vertexShader.kind = StageKind::Vertex;
    lit.vertexShader.assignment = analysis.hasVertexEntry
        ? StageAssignmentKind::InlineShader : StageAssignmentKind::GenericName;
    lit.vertexShader.genericName = analysis.hasVertexEntry ? QString() : "vs_sky";
    lit.vertexShader.entryPoint = analysis.hasVertexEntry ? "vs_main" : QString();
    lit.pixelShader.kind = StageKind::Pixel;
    lit.pixelShader.assignment = StageAssignmentKind::InlineShader;
    lit.pixelShader.baseName = "ps_sky";
    lit.pixelShader.source = result.sourceFileName;
    lit.pixelShader.sourceAssigned = true;
    lit.pixelShader.entryPoint = "ps_main";
    lit.pixelShader.defines = {"USE_EMISSIVE"};
    lit.pixelShader.definesAssigned = true;
    result.techset.techniques << lit;

    TechniqueModel unlit;
    unlit.names = {"unlit"};
    unlit.state = "disable + depthTestOnly";
    unlit.stateAssigned = true;
    unlit.source = "techsetdef_sky_latlong_hdr.hlsl";
    unlit.sourceAssigned = true;
    unlit.vertexShader.kind = StageKind::Vertex;
    unlit.vertexShader.assignment = StageAssignmentKind::GenericName;
    unlit.vertexShader.genericName = "vs_sky";
    unlit.pixelShader.kind = StageKind::Pixel;
    unlit.pixelShader.assignment = StageAssignmentKind::GenericName;
    unlit.pixelShader.genericName = "ps_sky";
    result.techset.techniques << unlit;

    if(result.confidence == AutomationConfidence::Guided)
        result.diagnostics.add(DiagnosticLevel::Warning, "ADAPTER_NEEDS_MAPPING",
            "One or more sky resources need an explicit role before package generation.");
    return result;
}

} // namespace bo3
