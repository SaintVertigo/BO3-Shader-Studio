#include "preview_package_session.h"

#include "bo3_techset_writer.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace bo3
{
namespace
{

PackageTarget packageTarget(ShaderPreviewMode target)
{
    switch(target)
    {
        case ShaderPreviewMode::PostFx: return PackageTarget::PostFx;
        case ShaderPreviewMode::Material: return PackageTarget::Material;
        case ShaderPreviewMode::Skybox: return PackageTarget::Skybox;
        case ShaderPreviewMode::Hlsl: break;
    }
    return PackageTarget::PostFx;
}

QString stateFingerprint(const PreviewPackageState& state,
                         const QMap<QString, QString>& overrides)
{
    QStringList parts{
        QString::number(static_cast<int>(state.target)),
        QString::number(static_cast<int>(state.configuration)),
        QString::number(static_cast<int>(state.origin)),
        QString::number(static_cast<int>(state.confidence)),
        state.sourceFileName,
        state.entryPoint,
        state.profile,
        state.adaptedSource,
        state.temporaryTechsetText
    };
    for(auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        parts << QString("mapping:%1=%2").arg(it.key(), it.value());
    for(const PackageResourceMapping& mapping : state.adapter.mappings)
        parts << QString("resolved:%1=%2:%3")
                     .arg(mapping.resourceName, mapping.selectedBinding)
                     .arg(static_cast<int>(mapping.role));
    return QString::fromLatin1(QCryptographicHash::hash(
        parts.join(QChar('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
}

void retargetPackageSource(TechsetModel& techset, const QString& previousName,
                           const QString& newName)
{
    for(TechniqueModel& technique : techset.techniques)
    {
        if(technique.source == previousName) technique.source = newName;
        if(technique.vertexShader.source == previousName)
            technique.vertexShader.source = newName;
        if(technique.pixelShader.source == previousName)
            technique.pixelShader.source = newName;
    }
}

bool containsMotionDriver(const QString& expression)
{
    static const QRegularExpression driverRe(
        R"(\b(?:iTime|iTimeDelta|iFrame|iMouse|mouse|keyboard|keyState|GetTime|gameTime)\b)",
        QRegularExpression::CaseInsensitiveOption);
    return driverRe.match(expression).hasMatch();
}

bool isCoordinateLikeName(const QString& name)
{
    static const QRegularExpression coordinateNameRe(
        R"(^(?:fragCoord|screen(?:UV|Pos|Position|Coord|Coords)?|uv[0-9]*|st|p|q|pos[0-9]*|position|coord[0-9]*|coords[0-9]*)$)",
        QRegularExpression::CaseInsensitiveOption);
    return coordinateNameRe.match(name).hasMatch();
}

bool isScreenCoordinateExpression(const QString& expression,
                                  const QSet<QString>& knownAliases)
{
    static const QRegularExpression directRe(
        R"(\b(?:fragCoord|GLSL_FRAGCOORD|screenUV|screenUv|screenPos|screenPosition)\b|\b(?:input|pixel)\s*\.\s*(?:position|texcoord|texCoords)\b)",
        QRegularExpression::CaseInsensitiveOption);
    if(directRe.match(expression).hasMatch()) return true;
    for(const QString& alias : knownAliases)
    {
        if(QRegularExpression(QString(R"(\b%1\b)")
                .arg(QRegularExpression::escape(alias)),
                QRegularExpression::CaseInsensitiveOption).match(expression).hasMatch())
            return true;
    }
    return false;
}

bool isCameraLikeName(const QString& name)
{
    static const QRegularExpression cameraNameRe(
        R"(^(?:cam(?:era)?(?:Pos(?:ition)?|Dir(?:ection)?|Target)?|eye(?:Pos(?:ition)?)?|rayOrigin|rayDir(?:ection)?|look(?:At|Dir)?|view(?:Pos|Dir)?|yaw|pitch|orbit)$)",
        QRegularExpression::CaseInsensitiveOption);
    return cameraNameRe.match(name).hasMatch();
}

QString replacementComment(const QString& original, const QString& variable)
{
    QString replacement = QString("/* BO3_PREVIEWER_REMOVED_CAMERA_INPUT_MOVEMENT: %1 */")
                              .arg(variable);
    const int newlineCount = original.count('\n');
    for(int i = 0; i < newlineCount; ++i) replacement += '\n';
    return replacement;
}

} // namespace

ShaderMotionTransformResult removeShaderCameraInputMovement(const QString& source)
{
    ShaderMotionTransformResult result;
    result.source = source;
    if(source.trimmed().isEmpty()) return result;

    // Converted 360° Shadertoy skies already have their authored camera ray
    // replaced with BO3 skyDirection. Running the generic camera/input removal
    // pass again can delete helper camera math that the procedural environment
    // still uses for world-space shading, so treat the explicit sky adapter as
    // authoritative instead of destructively rewriting it a second time.
    if(source.contains("BO3_PREVIEWER_SKY_SELF_CAMERA", Qt::CaseInsensitive))
    {
        result.diagnostics.add(DiagnosticLevel::Info,
            "ADAPTER_SHADER_CAMERA_INPUT_MOVEMENT_ALREADY_REPLACED",
            "This converted self-camera sky already uses BO3 skyDirection for its view ray. Generic camera/input movement removal was skipped to avoid damaging the 360-degree environment shader.");
        return result;
    }

    // First identify variables that directly represent fullscreen coordinates.
    // We only rewrite time/input-driven updates to these aliases; ordinary
    // time-dependent shading elsewhere remains untouched.
    QSet<QString> screenAliases;
    bool discovered = true;
    const QRegularExpression declarationRe(
        R"(\b(?:float2|half2)\s+([A-Za-z_]\w*)\s*=\s*([^;]+);)",
        QRegularExpression::CaseInsensitiveOption);
    while(discovered)
    {
        discovered = false;
        auto it = declarationRe.globalMatch(source);
        while(it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            const QString name = match.captured(1);
            if(screenAliases.contains(name)) continue;
            if(!isCoordinateLikeName(name)) continue;
            if(isScreenCoordinateExpression(match.captured(2), screenAliases))
            {
                screenAliases.insert(name);
                discovered = true;
            }
        }
    }

    struct Span
    {
        qsizetype start = 0;
        qsizetype length = 0;
        QString variable;
        QString original;
    };
    QVector<Span> spans;

    // Catch the common Shadertoy pattern used by the oil-paint shader:
    //   float2 pos = fragCoord.xy;
    //   pos += sin(iTime...) * ...;
    // Also handles screen-UV aliases and input-driven variants.
    const QRegularExpression updateRe(
        R"((?m)^\s*([A-Za-z_]\w*)(?:\s*\.\s*[xyzwrgba]{1,4})?\s*(\+=|-=)\s*([^;]+);)",
        QRegularExpression::CaseInsensitiveOption);
    auto updates = updateRe.globalMatch(source);
    while(updates.hasNext())
    {
        const QRegularExpressionMatch match = updates.next();
        const QString variable = match.captured(1);
        const QString expression = match.captured(3);
        if(!containsMotionDriver(expression)) continue;
        if(!screenAliases.contains(variable) && !isCameraLikeName(variable)) continue;
        spans.push_back({match.capturedStart(), match.capturedLength(), variable,
                         match.captured(0)});
    }

    // A less common equivalent form is `uv = uv + timeDrivenOffset;`.
    const QRegularExpression reassignmentRe(
        R"((?m)^\s*([A-Za-z_]\w*)\s*=\s*\1\s*([+-])\s*([^;]+);)",
        QRegularExpression::CaseInsensitiveOption);
    auto reassignments = reassignmentRe.globalMatch(source);
    while(reassignments.hasNext())
    {
        const QRegularExpressionMatch match = reassignments.next();
        const QString variable = match.captured(1);
        const QString expression = match.captured(3);
        if(!containsMotionDriver(expression)) continue;
        if(!screenAliases.contains(variable) && !isCameraLikeName(variable)) continue;
        bool overlaps = false;
        for(const Span& span : spans)
            overlaps = overlaps || !(match.capturedEnd() <= span.start ||
                                     match.capturedStart() >= span.start + span.length);
        if(!overlaps)
            spans.push_back({match.capturedStart(), match.capturedLength(), variable,
                             match.captured(0)});
    }

    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b)
    {
        return a.start > b.start;
    });
    for(const Span& span : spans)
    {
        result.source.replace(span.start, span.length,
                              replacementComment(span.original, span.variable));
        result.changed = true;
        result.diagnostics.add(DiagnosticLevel::Info,
            "ADAPTER_SHADER_CAMERA_INPUT_MOVEMENT_REMOVED",
            QString("Removed a time/input-driven movement update to '%1' from the adapted shader copy; the original source was left unchanged.")
                .arg(span.variable));
    }

    // Report camera/input-driven code we noticed but could not safely rewrite.
    // This remains a warning only; we would rather preserve uncertain authored
    // behavior than delete animation that is not actually camera movement.
    static const QRegularExpression uncertainRe(
        R"((?m)^\s*(?:float[234]?|half[234]?|int[234]?|uint[234]?|bool[234]?)?\s*([A-Za-z_]\w*)[^;=]*=\s*([^;]*(?:iMouse|mouse|keyboard|GetTime\s*\(|iTime|gameTime)[^;]*);)",
        QRegularExpression::CaseInsensitiveOption);
    auto uncertain = uncertainRe.globalMatch(source);
    while(uncertain.hasNext())
    {
        const QRegularExpressionMatch match = uncertain.next();
        const QString variable = match.captured(1);
        if(!isCameraLikeName(variable)) continue;
        bool alreadyRemoved = false;
        for(const Span& span : spans)
            if(span.variable.compare(variable, Qt::CaseInsensitive) == 0)
                alreadyRemoved = true;
        if(alreadyRemoved) continue;
        result.diagnostics.add(DiagnosticLevel::Warning,
            "ADAPTER_SHADER_CAMERA_INPUT_MOVEMENT_UNCERTAIN",
            QString("Detected input/time-driven camera-like assignment to '%1', but it was preserved because removing the complete assignment could change unrelated shader semantics.")
                .arg(variable));
    }

    return result;
}

bool PreviewPackageState::hasGuidedMappings() const
{
    for(const PackageResourceMapping& mapping : adapter.mappings)
        if(mapping.confidence == AutomationConfidence::Guided) return true;
    return false;
}

bool PreviewPackageState::canPersist() const
{
    return origin == PreviewPackageOrigin::TemporaryAdapted &&
           confidence == AutomationConfidence::Auto &&
           !adaptedSource.isEmpty() && !temporaryTechsetText.isEmpty();
}

const PreviewPackageState& PreviewPackageSession::rebuild(const PreviewPackageRequest& request)
{
    request_ = request;
    state_ = {};
    state_.generation = nextGeneration_++;
    state_.target = request.target;
    state_.configuration = request.configuration;
    state_.source = request.source;
    state_.entryPoint = request.entryPoint.trimmed().isEmpty()
        ? QString("ps_main") : request.entryPoint.trimmed();
    state_.profile = request.profile.trimmed().isEmpty()
        ? QString("ps_5_0") : request.profile.trimmed();
    state_.sourceFileName = request.sourceFileName.trimmed().isEmpty()
        ? QString("temporary_preview.hlsl") : request.sourceFileName;

    if(request.target == ShaderPreviewMode::Hlsl)
    {
        state_.origin = PreviewPackageOrigin::TemporaryGeneric;
        state_.confidence = AutomationConfidence::Auto;
        if(request.removeShaderCameraInputMovement)
            state_.adaptedSource = removeShaderCameraInputMovement(request.source).source;
        else
            state_.adaptedSource = request.source;
        state_.packageValidationRequired = false;
        state_.readyForPreviewCompilation = !request.source.trimmed().isEmpty();
        state_.stateFingerprint = stateFingerprint(state_, request.mappingOverrides);
        return state_;
    }

    if(!request.adapterSourceSupported)
    {
        state_.origin = PreviewPackageOrigin::TemporaryAdapted;
        state_.confidence = AutomationConfidence::Unsupported;
        state_.packageValidationRequired = true;
        state_.adapter.target = packageTarget(request.target);
        state_.adapter.confidence = AutomationConfidence::Unsupported;
        state_.adapter.diagnostics.add(DiagnosticLevel::Error,
            "PREVIEW_ADAPTER_PREPARATION",
            request.adapterSourceError.isEmpty()
                ? QString("The selected target cannot safely adapt this shader.")
                : request.adapterSourceError);
        state_.stateFingerprint = stateFingerprint(state_, request.mappingOverrides);
        return state_;
    }

    PackageAdapterRequest adapterRequest;
    adapterRequest.target = packageTarget(request.target);
    adapterRequest.configuration = request.configuration;
    const QString adapterInput = request.adapterSource.isEmpty()
        ? request.source : request.adapterSource;
    ShaderMotionTransformResult motionTransform;
    if(request.removeShaderCameraInputMovement)
        motionTransform = removeShaderCameraInputMovement(adapterInput);
    else
        motionTransform.source = adapterInput;
    adapterRequest.source = motionTransform.source;
    adapterRequest.sourceFileName = state_.sourceFileName;
    adapterRequest.mappingOverrides = request.mappingOverrides;
    state_.adapter = adaptShaderPackage(adapterRequest);
    state_.adapter.diagnostics.append(motionTransform.diagnostics);
    state_.origin = PreviewPackageOrigin::TemporaryAdapted;
    state_.confidence = state_.adapter.confidence;
    state_.adaptedSource = state_.adapter.adaptedSource;
    state_.sourceFileName = state_.adapter.sourceFileName;
    state_.packageValidationRequired = true;
    state_.readyForPreviewCompilation = state_.confidence != AutomationConfidence::Unsupported &&
                                        !state_.adaptedSource.trimmed().isEmpty();
    if(state_.confidence == AutomationConfidence::Auto)
        state_.temporaryTechsetText = serializeTechset(state_.adapter.techset);
    state_.stateFingerprint = stateFingerprint(state_, request.mappingOverrides);
    return state_;
}

const PreviewPackageState& PreviewPackageSession::applyMapping(
    const QString& resourceName, const QString& selectedBinding)
{
    request_.mappingOverrides[resourceName] = selectedBinding;
    return rebuild(request_);
}

bool PreviewPackageSession::pruneOptimizedOutPostFxResources(
    const QSet<QString>& reflectedResourceNames)
{
    if(state_.origin != PreviewPackageOrigin::TemporaryAdapted ||
       state_.target != ShaderPreviewMode::PostFx ||
       state_.confidence != AutomationConfidence::Auto)
        return false;

    QSet<QString> reflectedLower;
    for(const QString& name : reflectedResourceNames)
        reflectedLower.insert(name.toLower());

    QSet<QString> prunedParameterNames;
    QVector<PackageResourceMapping> keptMappings;
    keptMappings.reserve(state_.adapter.mappings.size());
    for(const PackageResourceMapping& mapping : state_.adapter.mappings)
    {
        if(mapping.resourceKind != CompiledResourceKind::Texture &&
           mapping.resourceKind != CompiledResourceKind::Sampler)
        {
            keptMappings << mapping;
            continue;
        }
        if(mapping.role == PackageResourceRole::Ignore ||
           mapping.selectedBinding.compare("ignore", Qt::CaseInsensitive) == 0)
        {
            keptMappings << mapping;
            continue;
        }

        QString compiledName = mapping.resourceName;
        if(mapping.resourceKind == CompiledResourceKind::Sampler &&
           !mapping.selectedBinding.isEmpty() &&
           mapping.selectedBinding.compare("keep", Qt::CaseInsensitive) != 0)
            compiledName = mapping.selectedBinding;

        if(reflectedLower.contains(compiledName.toLower()))
        {
            keptMappings << mapping;
            continue;
        }

        prunedParameterNames.insert(compiledName.toLower());
        // Texture bindings always use the authored texture name even when their
        // selected role is resolvedScene/floatZ/materialImage.
        if(mapping.resourceKind == CompiledResourceKind::Texture)
            prunedParameterNames.insert(mapping.resourceName.toLower());
        state_.adapter.diagnostics.add(DiagnosticLevel::Info,
            "ADAPTER_OPTIMIZED_RESOURCE_PRUNED",
            QString("Removed generated package resource '%1' because optimized ps_5_0 bytecode does not expose it.")
                .arg(compiledName));
    }

    if(prunedParameterNames.isEmpty()) return false;

    state_.adapter.mappings = keptMappings;

    QVector<ParameterModel> keptParameters;
    keptParameters.reserve(state_.adapter.techset.parameters.size());
    for(const ParameterModel& parameter : state_.adapter.techset.parameters)
    {
        if((parameter.kind == ParameterKind::Texture || parameter.kind == ParameterKind::Sampler) &&
           prunedParameterNames.contains(parameter.name.toLower()))
            continue;
        keptParameters << parameter;
    }
    state_.adapter.techset.parameters = keptParameters;

    for(TechniqueModel& technique : state_.adapter.techset.techniques)
    {
        QVector<StageResourceBindingModel> keptBindings;
        keptBindings.reserve(technique.pixelShader.resourceBindings.size());
        for(const StageResourceBindingModel& binding : technique.pixelShader.resourceBindings)
        {
            if(prunedParameterNames.contains(binding.parameterName.toLower())) continue;
            keptBindings << binding;
        }
        technique.pixelShader.resourceBindings = keptBindings;
    }

    state_.temporaryTechsetText = serializeTechset(state_.adapter.techset);
    state_.generation = nextGeneration_++;
    state_.stateFingerprint = stateFingerprint(state_, request_.mappingOverrides);
    return true;
}

PackageAdapterResult PreviewPackageSession::persistentResult(
    const QString& sourceFileName) const
{
    if(!state_.canPersist() || sourceFileName.trimmed().isEmpty()) return {};
    PackageAdapterResult result = state_.adapter;
    const QString previousName = result.sourceFileName;
    result.sourceFileName = sourceFileName;
    result.techset.sourceName = QFileInfo(sourceFileName).completeBaseName() +
                                ".techsetdef";
    retargetPackageSource(result.techset, previousName, result.sourceFileName);
    return result;
}

void PreviewPackageSession::clear()
{
    request_ = {};
    state_ = {};
    state_.generation = nextGeneration_++;
}

QString toString(PreviewPackageOrigin origin)
{
    switch(origin)
    {
        case PreviewPackageOrigin::None: return "None";
        case PreviewPackageOrigin::TemporaryGeneric: return "AUTO (temporary HLSL harness)";
        case PreviewPackageOrigin::TemporaryAdapted: return "AUTO (temporary BO3 package)";
    }
    return "None";
}

} // namespace bo3
