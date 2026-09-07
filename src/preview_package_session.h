#pragma once

#include "bo3_package_adapter.h"
#include "hlsl_preview_mode.h"

namespace bo3
{

enum class PreviewPackageOrigin
{
    None,
    TemporaryGeneric,
    TemporaryAdapted
};

struct PreviewPackageRequest
{
    ShaderPreviewMode target = ShaderPreviewMode::Hlsl;
    PackageConfiguration configuration = PackageConfiguration::Runtime;
    QString source;
    QString adapterSource;
    QString sourceFileName;
    QString entryPoint = "ps_main";
    QString profile = "ps_5_0";
    QMap<QString, QString> mappingOverrides;
    // Non-destructive source transform used by temporary/adapted preview
    // packages (and persisted copies derived from them). This request itself never
    // rewrites the editor/file; the UI also exposes an explicit, undoable command
    // when the user intentionally wants to apply the transform to editor HLSL.
    bool removeShaderCameraInputMovement = false;
    bool adapterSourceSupported = true;
    QString adapterSourceError;
};

struct ShaderMotionTransformResult
{
    QString source;
    ValidationResult diagnostics;
    bool changed = false;
};

// Conservatively removes common whole-screen / camera-input motion while leaving
// unrelated time animation intact. The normal preview/package path uses this on an
// adapted copy; callers may also explicitly apply the returned source to an editor.
// This is intentionally not a generic "remove iTime" pass.
ShaderMotionTransformResult removeShaderCameraInputMovement(const QString& source);

struct PreviewPackageState
{
    quint64 generation = 0;
    ShaderPreviewMode target = ShaderPreviewMode::Hlsl;
    PackageConfiguration configuration = PackageConfiguration::Runtime;
    PreviewPackageOrigin origin = PreviewPackageOrigin::None;
    AutomationConfidence confidence = AutomationConfidence::Unsupported;
    QString source;
    QString adaptedSource;
    QString sourceFileName;
    QString entryPoint;
    QString profile;
    QString temporaryTechsetText;
    QString stateFingerprint;
    PackageAdapterResult adapter;

    bool packageValidationRequired = false;
    bool readyForPreviewCompilation = false;

    bool isTemporary() const { return origin != PreviewPackageOrigin::None; }
    bool hasGuidedMappings() const;
    bool canPersist() const;
};

class PreviewPackageSession
{
public:
    const PreviewPackageState& rebuild(const PreviewPackageRequest& request);
    const PreviewPackageState& applyMapping(const QString& resourceName,
                                            const QString& selectedBinding);
    PackageAdapterResult persistentResult(const QString& sourceFileName) const;
    // Temporary PostFX packages are initially source-driven. Once the preview
    // pixel shader has been optimized by FXC, remove generated texture/sampler
    // parameters and Runtime bindings that are absent from compiled reflection.
    // This keeps the saved package identical to the bytecode contract BO3 sees.
    bool pruneOptimizedOutPostFxResources(const QSet<QString>& reflectedResourceNames);
    void clear();

    const PreviewPackageState& state() const { return state_; }
    const PreviewPackageRequest& request() const { return request_; }

private:
    quint64 nextGeneration_ = 1;
    PreviewPackageRequest request_;
    PreviewPackageState state_;
};

QString toString(PreviewPackageOrigin origin);

} // namespace bo3
