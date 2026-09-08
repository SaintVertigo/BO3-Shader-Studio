#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace bo3::glsl
{
// Core GLSL -> HLSL conversion API. The implementation intentionally lives in
// its own translation unit so converter-only fixes do not force recompilation
// of BO3 Shader Studio's very large main/UI translation unit.
struct GlslPostFxSceneOrientationAnalysis
{
    bool keepSceneY = false;
    QString reason;
};

struct GlslSkySourceAnalysis
{
    int recommendedMode = 1; // 1 = image-space/lat-long, 2 = self-camera/view-ray.
    int selfCameraScore = 0;
    QString rayVariable;
    QString reason;
};

QString convertGlslSyntax(QString source, QStringList& notes,
                          QSet<int>& usedChannels, QStringList& varyingAliases);

// Exposed for the regression suite and wrapper generators. Most converter
// implementation helpers remain private to glsl_converter_core.cpp.
QString lowerRuntimeDependentGlslGlobals(QString source, QStringList& notes);
QString normalizeConstantLoopsForFxc(QString source, QStringList& notes);
QString glslCompatibilityHelpers(const QString& converted = QString(),
                                 bool emitFullLibrary = false);
QString namespaceConvertedGlslUserFunctions(QString source, QStringList& notes);

GlslPostFxSceneOrientationAnalysis analyzeGlslPostFxSceneOrientation(const QString& source);
QString makeBo3PostfxFromGlsl(const QString& converted, const QSet<int>& channels,
                              const QStringList& varyingAliases,
                              int sceneOrientationMode = 0,
                              const QString& originalGlsl = QString());
QString makeBo3MaterialFromGlsl(const QString& converted, const QSet<int>& channels,
                                const QStringList& varyingAliases, int surfaceMode);

GlslSkySourceAnalysis analyzeGlslSkySource(const QString& source);
QString makeBo3SkyFromGlsl(const QString& converted, const QSet<int>& channels,
                           const QStringList& varyingAliases, int requestedSkySourceMode,
                           const QString& originalGlsl,
                           QStringList* converterNotes = nullptr);
} // namespace bo3::glsl
