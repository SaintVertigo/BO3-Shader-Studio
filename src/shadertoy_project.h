#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace ShadertoyProject
{
constexpr int ChannelCount = 4;

enum class ChannelUsage
{
    AssignedUnused,
    Referenced,
    Used,
    PossiblyUsed
};

enum class ProjectMode
{
    Preserve,
    Standalone,
    Custom
};

enum class ChannelMode
{
    Auto,
    Preserve,
    Procedural,
    Neutral,
    Remove
};

struct ChannelAnalysis
{
    ChannelUsage usage = ChannelUsage::AssignedUnused;
    bool directRead = false;
    bool indirectRead = false;
    QStringList reasons;
};

struct PassAnalysis
{
    std::array<ChannelAnalysis, ChannelCount> channels;
};

struct ChannelBinding
{
    QString kind = "none";
    int bufferPassIndex = -1;
    QString source;
    QString resourceId;
    QString originalType;
    QString filterMode;
    QString wrapMode;
    QString vflip;
    QString srgb;
    QString assetUrl;
    QString cachedFile;
    QString assetError;
    ChannelMode requestedMode = ChannelMode::Auto;
    ChannelAnalysis analysis;
};

struct HandlingDecision
{
    QString effectiveMode = "Ignored";
    bool preserveInput = false;
    bool rewriteSampling = false;
    bool proceduralNoise = false;
    bool safeToRemove = false;
    QString warning;
};

struct TransformResult
{
    QString commonSource;
    QString passSource;
    QStringList notes;
    std::array<HandlingDecision, ChannelCount> decisions;
};

QString usageName(ChannelUsage usage);
QString projectModeName(ProjectMode mode);
QString channelModeName(ChannelMode mode);
ProjectMode projectModeFromString(const QString& value);
ChannelMode channelModeFromString(const QString& value);

PassAnalysis analyze(const QString& commonSource, const QString& passSource);
HandlingDecision decide(const ChannelBinding& binding, ProjectMode projectMode,
                        int passIndex);
TransformResult transform(const QString& commonSource, const QString& passSource,
                          const QVector<ChannelBinding>& bindings,
                          ProjectMode projectMode, int passIndex);

bool shouldFetchAsset(const ChannelBinding& binding, ProjectMode projectMode,
                      int passIndex, bool explicitlyRequested = false);
bool isGenericNoiseAsset(const ChannelBinding& binding);
QString passAnalysisSummary(const QVector<ChannelBinding>& bindings,
                            ProjectMode projectMode, int passIndex);
}
