#pragma once

#include "bo3_package.h"

namespace bo3
{

struct PackageValidationOptions
{
    bool unreflectedStageBindingIsError = true;
    bool unreflectedTopLevelParameterIsWarning = false;
    bool validateGlobalConstants = true;
    QMap<QString, TextureDimension> codeTextureDimensions;
    QSet<QString> externallyBoundResources;
    QSet<QString> externallyBoundConstantBuffers;
};

PackageValidationOptions defaultPackageValidationOptions();

ValidationResult validatePackage(const BO3ShaderPackage& package,
                                 const PackageValidationOptions& options = defaultPackageValidationOptions());
ValidationResult validateStageResources(const TechsetModel& techset,
                                        const ResolvedShaderStage& stage,
                                        const CompiledShaderInterface& compiled,
                                        const PackageValidationOptions& options);
ValidationResult validateShaderInterfacePair(const CompiledShaderInterface& vertex,
                                             const CompiledShaderInterface& pixel);
ValidationResult validateSourcePath(const QString& logicalPath, const QString& source);

} // namespace bo3
