#include "bo3_package_validation.h"

#include "bo3_techset.h"

#include <QDir>
#include <QRegularExpression>

namespace bo3
{
namespace
{

bool isTextureResource(CompiledResourceKind kind)
{
    return kind == CompiledResourceKind::Texture;
}

bool isSystemResource(const CompiledResource& resource, const PackageValidationOptions& options)
{
    return options.externallyBoundResources.contains(resource.name) ||
           (resource.kind == CompiledResourceKind::ConstantBuffer &&
            options.externallyBoundConstantBuffers.contains(resource.name));
}

const StageResourceBindingModel* stageBinding(const ResolvedShaderStage& stage, const QString& name)
{
    for(auto it = stage.resourceBindings.crbegin(); it != stage.resourceBindings.crend(); ++it)
        if(it->parameterName == name) return &*it;
    return nullptr;
}

int expectedConstantSize(ParameterKind kind)
{
    switch(kind)
    {
        case ParameterKind::Float1:
        case ParameterKind::UInt1:
        case ParameterKind::Bool: return 4;
        case ParameterKind::Float2:
        case ParameterKind::UInt2: return 8;
        case ParameterKind::Float3:
        case ParameterKind::UInt3: return 12;
        case ParameterKind::Float4:
        case ParameterKind::UInt4:
        case ParameterKind::Color: return 16;
        default: return 0;
    }
}

bool numericKindMatches(ParameterKind kind, NumericKind reflected)
{
    if(reflected == NumericKind::Unknown) return true;
    switch(kind)
    {
        case ParameterKind::Float1:
        case ParameterKind::Float2:
        case ParameterKind::Float3:
        case ParameterKind::Float4:
        case ParameterKind::Color: return reflected == NumericKind::Float;
        case ParameterKind::UInt1:
        case ParameterKind::UInt2:
        case ParameterKind::UInt3:
        case ParameterKind::UInt4: return reflected == NumericKind::UInt;
        case ParameterKind::Bool: return reflected == NumericKind::SInt || reflected == NumericKind::UInt;
        default: return true;
    }
}

QString resourceDescription(const CompiledResource& resource)
{
    return QString("%1 '%2' at %3%4")
        .arg(toString(resource.kind), resource.name,
             resource.kind == CompiledResourceKind::Sampler ? "s" :
             resource.kind == CompiledResourceKind::ConstantBuffer ? "b" : "t")
        .arg(resource.bindPoint);
}

TextureDimension declaredTextureDimension(const QMap<QString, QString>& properties)
{
    const QString dimension = properties.value("dimension").trimmed().toLower();
    if(dimension == "2d" || dimension == "texture2d") return TextureDimension::Texture2D;
    if(dimension == "3d" || dimension == "texture3d") return TextureDimension::Texture3D;
    if(dimension == "cube" || dimension == "texturecube") return TextureDimension::TextureCube;

    const QString semantic = properties.value("semantic").trimmed().toLower();
    const QSet<QString> proven2dSemantics = {
        "2d", "diffusemap", "normalmap", "revealmap", "specularmap",
        "glossmap", "occlusionmap"
    };
    if(proven2dSemantics.contains(semantic)) return TextureDimension::Texture2D;

    const QString image = properties.value("image").toLower();
    if(image.contains("_cube")) return TextureDimension::TextureCube;
    return TextureDimension::Unknown;
}

} // namespace

PackageValidationOptions defaultPackageValidationOptions()
{
    PackageValidationOptions options;
    options.codeTextureDimensions = {
        {"resolvedScene", TextureDimension::Texture2D},
        {"resolvedPostSun", TextureDimension::Texture2D},
        {"frameBuffer", TextureDimension::Texture2D},
        {"sceneVelocity", TextureDimension::Texture2D},
        {"floatZ", TextureDimension::Texture2D}
    };
    options.externallyBoundConstantBuffers = {
        "PerSceneConsts", "LightingGlobals", "GenericsCBuffer", "PostFxCBuffer",
        "MotionVectorParams"
    };
    options.externallyBoundResources = {
        "modelInstanceBuffer", "boneMatrixBuffer", "boneMatrixBufferPrevFrame",
        "shaderConstantSetBuffer", "gpuSkinBase", "gpuSkinPos", "gpuSkinQuat"
    };
    return options;
}

ValidationResult validateStageResources(const TechsetModel& techset,
                                        const ResolvedShaderStage& stage,
                                        const CompiledShaderInterface& compiled,
                                        const PackageValidationOptions& options)
{
    ValidationResult result;
    QMap<QString, const CompiledResource*> reflectedByName;
    QMap<QString, QString> registerOwners;
    for(const CompiledResource& resource : compiled.resources)
    {
        reflectedByName[resource.name] = &resource;
        const QString slot = QString("%1:%2")
            .arg(static_cast<int>(resource.kind)).arg(resource.bindPoint);
        if(registerOwners.contains(slot))
            result.add(DiagnosticLevel::Error, "PACKAGE_CONFLICTING_REGISTER",
                QString("%1 conflicts with resource '%2' at the same compiled register.")
                    .arg(resourceDescription(resource), registerOwners.value(slot)));
        else registerOwners[slot] = resource.name;
    }

    QMap<QString, const StageResourceBindingModel*> explicitBindings;
    for(const StageResourceBindingModel& binding : stage.resourceBindings)
    {
        if(explicitBindings.contains(binding.parameterName))
        {
            const StageResourceBindingModel* previous = explicitBindings.value(binding.parameterName);
            if(previous->valueKind != binding.valueKind || previous->valueName != binding.valueName)
                result.add(DiagnosticLevel::Error, "PACKAGE_CONFLICTING_BINDING",
                    QString("Shader parameter '%1' has conflicting stage-local bindings.").arg(binding.parameterName),
                    binding.location);
        }
        explicitBindings[binding.parameterName] = &binding;
    }

    for(const CompiledResource& resource : compiled.resources)
    {
        if(resource.kind == CompiledResourceKind::ConstantBuffer || isSystemResource(resource, options)) continue;
        if(resource.kind != CompiledResourceKind::Texture && resource.kind != CompiledResourceKind::Sampler)
        {
            result.add(DiagnosticLevel::Unknown, "PACKAGE_UNSUPPORTED_RESOURCE_KIND",
                       QString("%1 is not yet modeled by the BO3 techset validator.").arg(resourceDescription(resource)));
            continue;
        }

        const StageResourceBindingModel* binding = stageBinding(stage, resource.name);
        const ParameterModel* parameter = techset.findParameter(resource.name);
        if(resource.kind == CompiledResourceKind::Sampler)
        {
            const bool explicitSampler = binding && binding->valueKind == BindingValueKind::Sampler;
            if(!explicitSampler && (!parameter || parameter->kind != ParameterKind::Sampler))
                result.add(DiagnosticLevel::Error, "PACKAGE_MISSING_SAMPLER",
                    QString("Compiled %1 has no matching Sampler(\"%2\") or stage-local sampler binding.")
                        .arg(resourceDescription(resource), resource.name));
        }
        else if(isTextureResource(resource.kind))
        {
            const bool explicitTexture = binding &&
                (binding->valueKind == BindingValueKind::Texture || binding->valueKind == BindingValueKind::CodeTexture);
            if(!explicitTexture && (!parameter || parameter->kind != ParameterKind::Texture))
            {
                result.add(DiagnosticLevel::Error, "PACKAGE_MISSING_TEXTURE",
                    QString("Compiled %1 has no matching Texture(\"%2\") or stage-local CodeTexture/Texture binding.")
                        .arg(resourceDescription(resource), resource.name));
                continue;
            }
            if(binding && binding->valueKind == BindingValueKind::CodeTexture)
            {
                if(!options.codeTextureDimensions.contains(binding->valueName))
                    result.add(DiagnosticLevel::Unknown, "PACKAGE_UNKNOWN_CODE_TEXTURE",
                        QString("CodeTexture '%1' has no proven BO3 resource type.").arg(binding->valueName), binding->location);
                else if(resource.dimension != TextureDimension::Unknown &&
                        options.codeTextureDimensions.value(binding->valueName) != resource.dimension)
                    result.add(DiagnosticLevel::Error, "PACKAGE_CODE_TEXTURE_TYPE",
                        QString("CodeTexture '%1' is %2 but compiled resource '%3' requires %4.")
                            .arg(binding->valueName, toString(options.codeTextureDimensions.value(binding->valueName)),
                                 resource.name, toString(resource.dimension)), binding->location);
            }
            else
            {
                const TextureDimension declared = binding && binding->valueKind == BindingValueKind::Texture
                    ? declaredTextureDimension(binding->properties)
                    : (parameter ? declaredTextureDimension(parameter->properties) : TextureDimension::Unknown);
                if(declared != TextureDimension::Unknown && resource.dimension != TextureDimension::Unknown &&
                   declared != resource.dimension)
                    result.add(DiagnosticLevel::Error, "PACKAGE_TEXTURE_TYPE",
                        QString("Techset texture binding '%1' is proven %2, but optimized HLSL requires %3.")
                            .arg(resource.name, toString(declared), toString(resource.dimension)),
                        binding ? binding->location : parameter->location);
            }
        }
    }

    for(const StageResourceBindingModel& binding : stage.resourceBindings)
    {
        if((binding.valueKind == BindingValueKind::Texture || binding.valueKind == BindingValueKind::CodeTexture ||
            binding.valueKind == BindingValueKind::Sampler) && !reflectedByName.contains(binding.parameterName))
        {
            result.add(options.unreflectedStageBindingIsError ? DiagnosticLevel::Error : DiagnosticLevel::Warning,
                "PACKAGE_BINDING_OPTIMIZED_OUT",
                QString("Techset binds '%1', but optimized %2 bytecode does not expose that parameter.")
                    .arg(binding.parameterName, toString(compiled.stage)), binding.location);
        }
    }

    if(options.validateGlobalConstants)
    {
        for(const CompiledConstantVariable& variable : compiled.constants)
        {
            if(variable.cbufferName != "$Globals" && variable.cbufferName != "_Globals") continue;
            ParameterModel parameter;
            if(!resolveParameter(techset, variable.name, parameter, &result))
            {
                result.add(DiagnosticLevel::Error, "PACKAGE_MISSING_CONSTANT",
                    QString("Compiled material constant '%1' in %2 is not declared by the techset.")
                        .arg(variable.name, variable.cbufferName));
                continue;
            }
            const int expected = expectedConstantSize(parameter.kind);
            if(expected > 0 && variable.size != expected)
                result.add(DiagnosticLevel::Error, "PACKAGE_CONSTANT_SIZE",
                    QString("Techset %1(\"%2\") is %3 bytes, but optimized HLSL exposes %4 bytes.")
                        .arg(toString(parameter.kind), variable.name).arg(expected).arg(variable.size), parameter.location);
            if(!numericKindMatches(parameter.kind, variable.numericKind))
                result.add(DiagnosticLevel::Error, "PACKAGE_CONSTANT_TYPE",
                    QString("Techset %1(\"%2\") is incompatible with the optimized HLSL numeric type.")
                        .arg(toString(parameter.kind), variable.name), parameter.location);
        }
    }

    if(options.unreflectedTopLevelParameterIsWarning)
    {
        for(const ParameterModel& parameter : techset.parameters)
        {
            const bool resource = parameter.kind == ParameterKind::Texture || parameter.kind == ParameterKind::Sampler;
            if(resource && !reflectedByName.contains(parameter.name))
                result.add(DiagnosticLevel::Warning, "PACKAGE_UNUSED_PARAMETER",
                    QString("Techset parameter '%1' is not exposed by the selected optimized %2.")
                        .arg(parameter.name, toString(compiled.stage)), parameter.location);
        }
    }
    return result;
}

ValidationResult validateShaderInterfacePair(const CompiledShaderInterface& vertex,
                                             const CompiledShaderInterface& pixel)
{
    ValidationResult result;
    if(vertex.stage != StageKind::Vertex || pixel.stage != StageKind::Pixel)
    {
        result.add(DiagnosticLevel::Error, "PACKAGE_WRONG_STAGE_PAIR", "VS/PS interface validation received the wrong shader stages.");
        return result;
    }
    for(const CompiledSemantic& input : pixel.inputs)
    {
        const bool rasterizerGenerated = input.name == "SV_ISFRONTFACE" || input.name == "SV_SAMPLEINDEX" ||
                                         input.name == "SV_PRIMITIVEID" || input.name == "SV_COVERAGE";
        if(rasterizerGenerated) continue;
        const CompiledSemantic* output = nullptr;
        for(const CompiledSemantic& candidate : vertex.outputs)
            if(candidate.name == input.name && candidate.index == input.index) { output = &candidate; break; }
        if(!output)
        {
            result.add(DiagnosticLevel::Error, "PACKAGE_SEMANTIC_MISSING",
                QString("Pixel shader expects %1%2, but the selected vertex shader does not output it.")
                    .arg(input.name).arg(input.index));
            continue;
        }
        if((input.mask & output->mask) != input.mask)
            result.add(DiagnosticLevel::Error, "PACKAGE_SEMANTIC_MASK",
                QString("Pixel shader uses components 0x%1 of %2%3, but the vertex shader outputs only 0x%4.")
                    .arg(input.mask, 0, 16).arg(input.name).arg(input.index).arg(output->mask, 0, 16));
        if(input.numericKind != NumericKind::Unknown && output->numericKind != NumericKind::Unknown &&
           input.numericKind != output->numericKind)
            result.add(DiagnosticLevel::Error, "PACKAGE_SEMANTIC_TYPE",
                QString("VS/PS numeric types differ for %1%2.").arg(input.name).arg(input.index));
    }
    return result;
}

ValidationResult validateSourcePath(const QString& logicalPath, const QString& source)
{
    ValidationResult result;
    QString normalized = QDir::fromNativeSeparators(logicalPath.trimmed());
    if(QDir::isAbsolutePath(normalized) || normalized.startsWith("../") || normalized.contains("/../"))
        result.add(DiagnosticLevel::Error, "PACKAGE_INVALID_SOURCE_PATH",
                   QString("Shader source path '%1' escapes the BO3 stable shader root.").arg(logicalPath));
    const QRegularExpression stockPostFxInclude(
        R"(^\s*#\s*include\s*[\"<]postfx/)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if(source.contains(stockPostFxInclude) && normalized.contains('/'))
        result.add(DiagnosticLevel::Error, "PACKAGE_STOCK_INCLUDE_NESTED_PATH",
            QString("Shader '%1' uses stock postfx includes from a nested path; this layout is confirmed to fail BO3 include resolution.").arg(logicalPath));
    return result;
}

ValidationResult validatePackage(const BO3ShaderPackage& package,
                                 const PackageValidationOptions& options)
{
    ValidationResult result;
    if(package.selectedTechnique.isEmpty())
        result.add(DiagnosticLevel::Error, "PACKAGE_NO_TECHNIQUE", "No BO3 technique is selected.");
    auto reportMissingInterface = [&](const ResolvedShaderStage& stage,
                                      const CompiledShaderInterface& compiled)
    {
        if(!compiled.bytecode.isEmpty()) return;
        const bool sourceIsBundled = !stage.source.isEmpty() && package.sources.contains(stage.source);
        if(sourceIsBundled)
        {
            result.add(DiagnosticLevel::Error,
                       stage.kind == StageKind::Vertex ? "PACKAGE_NO_COMPILED_VS" : "PACKAGE_NO_COMPILED_PS",
                       QString("The selected %1 source is bundled, but its optimized interface was not produced.")
                           .arg(toString(stage.kind)));
        }
        else
        {
            result.add(DiagnosticLevel::Unknown, "PACKAGE_EXTERNAL_STAGE_SOURCE",
                       QString("The selected %1 source '%2' is inherited from BO3/stock includes and is not bundled, so its optimized interface cannot be proven.")
                           .arg(toString(stage.kind), stage.source.isEmpty() ? stage.genericName : stage.source));
        }
    };
    reportMissingInterface(package.resolvedTechnique.vertexShader, package.vertexInterface);
    reportMissingInterface(package.resolvedTechnique.pixelShader, package.pixelInterface);

    if(!package.vertexInterface.bytecode.isEmpty())
        result.append(validateStageResources(package.techset, package.resolvedTechnique.vertexShader,
                                             package.vertexInterface, options));
    if(!package.pixelInterface.bytecode.isEmpty())
        result.append(validateStageResources(package.techset, package.resolvedTechnique.pixelShader,
                                             package.pixelInterface, options));
    if(!package.vertexInterface.bytecode.isEmpty() && !package.pixelInterface.bytecode.isEmpty())
    {
        result.append(validateShaderInterfacePair(package.vertexInterface, package.pixelInterface));
        QSet<QString> exposedResources;
        for(const CompiledResource& resource : package.vertexInterface.resources)
            exposedResources.insert(resource.name);
        for(const CompiledResource& resource : package.pixelInterface.resources)
            exposedResources.insert(resource.name);
        const TechniqueModel* auxiliaryUnlit = package.techset.findTechnique("unlit");
        const bool hasStockAuxiliaryUnlit = auxiliaryUnlit &&
            auxiliaryUnlit->source.compare("techsetdef_unlit_simple.hlsl", Qt::CaseInsensitive) == 0;
        for(const ParameterModel& parameter : package.techset.parameters)
        {
            const bool toolsgfxEditorColorMap =
                package.configuration == PackageConfiguration::Toolsgfx &&
                parameter.kind == ParameterKind::Texture &&
                parameter.name.compare("colorMap", Qt::CaseInsensitive) == 0 &&
                parameter.properties.value("image").trimmed().startsWith("Image(\"");
            const bool stockUnlitSampler =
                hasStockAuxiliaryUnlit &&
                parameter.kind == ParameterKind::Sampler &&
                parameter.name.compare("colorSampler", Qt::CaseInsensitive) == 0;
            if((parameter.kind == ParameterKind::Texture || parameter.kind == ParameterKind::Sampler) &&
               !exposedResources.contains(parameter.name) &&
               !toolsgfxEditorColorMap && !stockUnlitSampler)
                result.add(DiagnosticLevel::Warning, "PACKAGE_UNUSED_PARAMETER",
                    QString("Techset parameter '%1' is not exposed by either optimized shader in the selected technique.")
                        .arg(parameter.name), parameter.location);
        }
    }

    if(!package.resolvedTechnique.renderState.unknownTokens.isEmpty())
        result.add(DiagnosticLevel::Unknown, "PACKAGE_UNKNOWN_RENDER_STATE",
            QString("Unmodeled render-state token(s): %1.").arg(package.resolvedTechnique.renderState.unknownTokens.join(", ")));

    for(auto it = package.sources.cbegin(); it != package.sources.cend(); ++it)
        result.append(validateSourcePath(it->logicalPath, it->source));
    return result;
}

} // namespace bo3
