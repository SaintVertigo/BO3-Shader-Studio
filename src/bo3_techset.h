#pragma once

#include "bo3_package.h"

namespace bo3
{

struct TechsetParseOptions
{
    PackageConfiguration configuration = PackageConfiguration::Runtime;
    QMap<QString, QString> defines;
};

struct TechsetParseResult
{
    TechsetModel model;
    QString preprocessedSource;
    ValidationResult validation;
};

TechsetParseResult parseTechset(const QString& source, const QString& sourceName,
                                const TechsetParseOptions& options = {});

struct TechniqueResolutionResult
{
    ResolvedTechnique technique;
    ValidationResult validation;
    bool found = false;
};

TechniqueResolutionResult resolveTechnique(const TechsetModel& techset, const QString& selectedName);

bool resolveParameter(const TechsetModel& techset, const QString& name, ParameterModel& resolved,
                      ValidationResult* validation = nullptr);

} // namespace bo3
