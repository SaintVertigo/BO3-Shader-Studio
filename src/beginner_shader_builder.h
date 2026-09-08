#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace beginner
{

enum class Target
{
    PostFx = 0,
    Material = 1,
    Sky = 2
};

enum class ParameterKind
{
    Float,
    Color
};

struct ParameterDefinition
{
    QString key;
    QString name;
    QString description;
    ParameterKind kind = ParameterKind::Float;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.01;
    double defaultValue = 0.0;
    QColor defaultColor = QColor(Qt::white);
};

struct EffectDefinition
{
    QString id;
    QString name;
    QString description;
    QVector<Target> targets;
    QVector<ParameterDefinition> parameters;
};

struct Effect
{
    QString instanceId;
    QString typeId;
    bool enabled = true;
    QJsonObject parameters;
};

struct Project
{
    int version = 1;
    QString name = "Untitled Shader";
    Target target = Target::PostFx;
    QJsonObject settings;
    QVector<Effect> effects;
};

QString targetId(Target target);
QString targetName(Target target);
QString targetDescription(Target target);
bool targetFromId(const QString& id, Target& target);

const QVector<EffectDefinition>& effectDefinitions();
const EffectDefinition* effectDefinition(const QString& id);
bool supportsTarget(const EffectDefinition& definition, Target target);

Project makeDefaultProject(Target target);
Effect makeDefaultEffect(const QString& typeId);
Project makePreset(const QString& presetId, Target target);
QVector<QPair<QString, QString>> presetsForTarget(Target target);

QJsonObject projectToJson(const Project& project);
bool projectFromJson(const QJsonObject& object, Project& project, QString& error);

QString generateHlsl(const Project& project, QStringList* notes = nullptr);
QString projectSummary(const Project& project);
QString compatibilitySummary(const Project& project);

} // namespace beginner
