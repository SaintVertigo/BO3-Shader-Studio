#include "bo3_shader_reflection.h"

#include <d3d11shader.h>
#include <wrl/client.h>

namespace bo3
{
namespace
{
using Microsoft::WRL::ComPtr;

const char* profileFor(StageKind stage)
{
    switch(stage)
    {
        case StageKind::Vertex: return "vs_5_0";
        case StageKind::Pixel: return "ps_5_0";
        case StageKind::Geometry: return "gs_5_0";
        case StageKind::Hull: return "hs_5_0";
        case StageKind::Domain: return "ds_5_0";
        case StageKind::Compute: return "cs_5_0";
    }
    return "ps_5_0";
}

NumericKind numericKind(D3D_REGISTER_COMPONENT_TYPE type)
{
    switch(type)
    {
        case D3D_REGISTER_COMPONENT_FLOAT32: return NumericKind::Float;
        case D3D_REGISTER_COMPONENT_UINT32: return NumericKind::UInt;
        case D3D_REGISTER_COMPONENT_SINT32: return NumericKind::SInt;
        default: return NumericKind::Unknown;
    }
}

NumericKind numericKind(D3D_SHADER_VARIABLE_TYPE type)
{
    switch(type)
    {
        case D3D_SVT_FLOAT:
        case D3D_SVT_DOUBLE:
        case D3D_SVT_MIN8FLOAT:
        case D3D_SVT_MIN10FLOAT:
        case D3D_SVT_MIN16FLOAT: return NumericKind::Float;
        case D3D_SVT_UINT:
        case D3D_SVT_UINT8:
        case D3D_SVT_MIN16UINT: return NumericKind::UInt;
        case D3D_SVT_INT:
        case D3D_SVT_BOOL:
        case D3D_SVT_MIN12INT:
        case D3D_SVT_MIN16INT: return NumericKind::SInt;
        default: return NumericKind::Unknown;
    }
}

TextureDimension textureDimension(D3D_SRV_DIMENSION dimension)
{
    switch(dimension)
    {
        case D3D_SRV_DIMENSION_BUFFER:
        case D3D_SRV_DIMENSION_BUFFEREX: return TextureDimension::Buffer;
        case D3D_SRV_DIMENSION_TEXTURE1D: return TextureDimension::Texture1D;
        case D3D_SRV_DIMENSION_TEXTURE1DARRAY: return TextureDimension::Texture1DArray;
        case D3D_SRV_DIMENSION_TEXTURE2D: return TextureDimension::Texture2D;
        case D3D_SRV_DIMENSION_TEXTURE2DARRAY: return TextureDimension::Texture2DArray;
        case D3D_SRV_DIMENSION_TEXTURE2DMS: return TextureDimension::Texture2DMS;
        case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: return TextureDimension::Texture2DMSArray;
        case D3D_SRV_DIMENSION_TEXTURE3D: return TextureDimension::Texture3D;
        case D3D_SRV_DIMENSION_TEXTURECUBE: return TextureDimension::TextureCube;
        case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: return TextureDimension::TextureCubeArray;
        default: return TextureDimension::Unknown;
    }
}

NumericKind numericKind(D3D_RESOURCE_RETURN_TYPE type)
{
    switch(type)
    {
        case D3D_RETURN_TYPE_FLOAT:
        case D3D_RETURN_TYPE_DOUBLE:
        case D3D_RETURN_TYPE_UNORM:
        case D3D_RETURN_TYPE_SNORM: return NumericKind::Float;
        case D3D_RETURN_TYPE_UINT: return NumericKind::UInt;
        case D3D_RETURN_TYPE_SINT: return NumericKind::SInt;
        default: return NumericKind::Unknown;
    }
}

CompiledResourceKind resourceKind(D3D_SHADER_INPUT_TYPE type)
{
    switch(type)
    {
        case D3D_SIT_TEXTURE: return CompiledResourceKind::Texture;
        case D3D_SIT_SAMPLER: return CompiledResourceKind::Sampler;
        case D3D_SIT_CBUFFER:
        case D3D_SIT_TBUFFER: return CompiledResourceKind::ConstantBuffer;
        case D3D_SIT_STRUCTURED: return CompiledResourceKind::StructuredBuffer;
        case D3D_SIT_BYTEADDRESS: return CompiledResourceKind::ByteAddressBuffer;
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: return CompiledResourceKind::UnorderedAccess;
        default: return CompiledResourceKind::Unknown;
    }
}

CompiledSemantic reflectSemantic(const D3D11_SIGNATURE_PARAMETER_DESC& desc)
{
    CompiledSemantic result;
    result.name = QString::fromUtf8(desc.SemanticName ? desc.SemanticName : "").toUpper();
    result.index = static_cast<int>(desc.SemanticIndex);
    result.mask = static_cast<quint8>(desc.Mask);
    result.numericKind = numericKind(desc.ComponentType);
    result.systemValue = static_cast<int>(desc.SystemValueType);
    return result;
}

} // namespace

ValidationResult reflectCompiledShader(ID3DBlob* bytecode, StageKind stage,
                                       const QString& sourceName, const QString& entryPoint,
                                       CompiledShaderInterface& result)
{
    ValidationResult validation;
    if(!bytecode)
    {
        validation.add(DiagnosticLevel::Error, "HLSL_NO_BYTECODE", "No compiled shader bytecode was provided.");
        return validation;
    }

    ComPtr<ID3D11ShaderReflection> reflection;
    if(FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                         IID_PPV_ARGS(reflection.GetAddressOf()))))
    {
        validation.add(DiagnosticLevel::Error, "HLSL_REFLECTION_FAILED",
                       "D3DReflect could not inspect the optimized shader bytecode.", {sourceName, 0, 0});
        return validation;
    }

    D3D11_SHADER_DESC shaderDesc{};
    if(FAILED(reflection->GetDesc(&shaderDesc)))
    {
        validation.add(DiagnosticLevel::Error, "HLSL_REFLECTION_DESC_FAILED",
                       "Compiled shader metadata could not be read.", {sourceName, 0, 0});
        return validation;
    }

    result = {};
    result.stage = stage;
    result.sourceName = sourceName;
    result.entryPoint = entryPoint;
    result.bytecode = QByteArray(static_cast<const char*>(bytecode->GetBufferPointer()),
                                 static_cast<int>(bytecode->GetBufferSize()));
    result.instructionCount = static_cast<int>(shaderDesc.InstructionCount);

    for(UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if(FAILED(reflection->GetResourceBindingDesc(i, &bind)) || !bind.Name) continue;
        CompiledResource resource;
        resource.name = QString::fromUtf8(bind.Name);
        resource.kind = resourceKind(bind.Type);
        resource.dimension = textureDimension(bind.Dimension);
        resource.numericKind = numericKind(bind.ReturnType);
        resource.bindPoint = static_cast<int>(bind.BindPoint);
        resource.bindCount = static_cast<int>(bind.BindCount);
        result.resources << resource;
    }

    for(UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByIndex(i);
        if(!buffer) continue;
        D3D11_SHADER_BUFFER_DESC bufferDesc{};
        if(FAILED(buffer->GetDesc(&bufferDesc)) || !bufferDesc.Name) continue;
        for(UINT variableIndex = 0; variableIndex < bufferDesc.Variables; ++variableIndex)
        {
            ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(variableIndex);
            if(!variable) continue;
            D3D11_SHADER_VARIABLE_DESC variableDesc{};
            if(FAILED(variable->GetDesc(&variableDesc)) || !variableDesc.Name) continue;
            D3D11_SHADER_TYPE_DESC typeDesc{};
            ID3D11ShaderReflectionType* type = variable->GetType();
            if(type) type->GetDesc(&typeDesc);
            CompiledConstantVariable reflected;
            reflected.cbufferName = QString::fromUtf8(bufferDesc.Name);
            reflected.name = QString::fromUtf8(variableDesc.Name);
            reflected.startOffset = static_cast<int>(variableDesc.StartOffset);
            reflected.size = static_cast<int>(variableDesc.Size);
            reflected.numericKind = numericKind(typeDesc.Type);
            reflected.rows = static_cast<int>(typeDesc.Rows);
            reflected.columns = static_cast<int>(typeDesc.Columns);
            reflected.elements = static_cast<int>(typeDesc.Elements);
            result.constants << reflected;
        }
    }

    for(UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC desc{};
        if(SUCCEEDED(reflection->GetInputParameterDesc(i, &desc))) result.inputs << reflectSemantic(desc);
    }
    for(UINT i = 0; i < shaderDesc.OutputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC desc{};
        if(SUCCEEDED(reflection->GetOutputParameterDesc(i, &desc))) result.outputs << reflectSemantic(desc);
    }
    return validation;
}

ShaderCompileResult compileAndReflectShader(const ShaderCompileRequest& request)
{
    ShaderCompileResult result;
    const QByteArray source = request.source.toUtf8();
    const QByteArray sourceName = request.sourceName.toUtf8();
    const QByteArray entryPoint = request.entryPoint.toUtf8();

    QVector<QByteArray> defineNames;
    QVector<D3D_SHADER_MACRO> macros;
    defineNames.reserve(request.defines.size());
    macros.reserve(request.defines.size() + 1);
    for(const QString& define : request.defines)
    {
        defineNames << define.toUtf8();
        D3D_SHADER_MACRO macro{};
        macro.Name = defineNames.back().constData();
        macro.Definition = "1";
        macros << macro;
    }
    macros << D3D_SHADER_MACRO{nullptr, nullptr};

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.constData(), static_cast<SIZE_T>(source.size()),
                                  sourceName.constData(), macros.constData(), request.includeHandler,
                                  entryPoint.constData(), profileFor(request.stage), request.compileFlags, 0,
                                  bytecode.GetAddressOf(), errors.GetAddressOf());
    if(errors)
        result.compilerDiagnostics = QString::fromUtf8(static_cast<const char*>(errors->GetBufferPointer()),
                                                       static_cast<int>(errors->GetBufferSize())).trimmed();
    result.compilerDiagnostics.remove(QChar('\0'));
    if(FAILED(hr))
    {
        result.validation.add(DiagnosticLevel::Error, "HLSL_COMPILE_FAILED",
            result.compilerDiagnostics.isEmpty() ? "D3DCompile failed without diagnostics." : result.compilerDiagnostics,
            {request.sourceName, 0, 0});
        return result;
    }
    result.compiled = true;
    result.validation.append(reflectCompiledShader(bytecode.Get(), request.stage, request.sourceName,
                                                   request.entryPoint, result.shaderInterface));
    return result;
}

} // namespace bo3
