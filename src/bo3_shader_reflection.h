#pragma once

#include "bo3_package.h"

#include <d3dcompiler.h>

namespace bo3
{

struct ShaderCompileRequest
{
    QString source;
    QString sourceName;
    QString entryPoint;
    StageKind stage = StageKind::Pixel;
    QStringList defines;
    ID3DInclude* includeHandler = nullptr;
    unsigned int compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
};

struct ShaderCompileResult
{
    bool compiled = false;
    QString compilerDiagnostics;
    CompiledShaderInterface shaderInterface;
    ValidationResult validation;
};

ShaderCompileResult compileAndReflectShader(const ShaderCompileRequest& request);
ValidationResult reflectCompiledShader(ID3DBlob* bytecode, StageKind stage,
                                       const QString& sourceName, const QString& entryPoint,
                                       CompiledShaderInterface& result);

} // namespace bo3
