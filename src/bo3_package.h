#pragma once

#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace bo3
{

enum class PackageConfiguration
{
    Runtime,
    Toolsgfx
};

enum class DiagnosticLevel
{
    Info,
    Warning,
    Error,
    Unknown
};

enum class CompatibilityStatus
{
    Pass,
    Warning,
    Fail,
    Unknown
};

struct SourceLocation
{
    QString sourceName;
    int line = 0;
    int column = 0;
};

struct Diagnostic
{
    DiagnosticLevel level = DiagnosticLevel::Info;
    QString code;
    QString message;
    SourceLocation location;
};

struct ValidationResult
{
    QVector<Diagnostic> diagnostics;

    void add(DiagnosticLevel level, const QString& code, const QString& message,
             const SourceLocation& location = {});
    void append(const ValidationResult& other);
    bool hasErrors() const;
    bool hasWarnings() const;
    bool hasUnknowns() const;
    CompatibilityStatus status() const;
    QString toText() const;
};

enum class ParameterKind
{
    Unknown,
    Sampler,
    Texture,
    Float1,
    Float2,
    Float3,
    Float4,
    UInt1,
    UInt2,
    UInt3,
    UInt4,
    Bool,
    Color
};

struct TweakModel
{
    QString category;
    QString title;
    QString order;
    QMap<QString, QString> properties;
};

struct ParameterModel
{
    ParameterKind kind = ParameterKind::Unknown;
    QString name;
    QString baseName;
    QMap<QString, QString> properties;
    TweakModel tweak;
    bool hasTweak = false;
    SourceLocation location;
};

struct GlobalsModel
{
    QString category;
    QString renderFlagsText;
    QMap<QString, QString> renderFlags;
    QString availablePrefixes;
    QMap<QString, QString> properties;
    SourceLocation location;
};

enum class RenderBlendMode
{
    Unknown,
    Replace,
    Blend,
    Add,
    GBuffer,
    Fallback
};

struct RenderStateModel
{
    QString raw;
    QStringList tokens;
    RenderBlendMode blend = RenderBlendMode::Unknown;
    bool depth = false;
    bool noCull = false;
    bool decal = false;
    bool opaque = false;
    bool depthPrepass = false;
    bool rez = false;
    bool sky = false;
    QStringList unknownTokens;
};

enum class StageKind
{
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain,
    Compute
};

enum class StageAssignmentKind
{
    Unset,
    GenericName,
    InlineShader
};

enum class BindingValueKind
{
    Unknown,
    Texture,
    CodeTexture,
    Sampler,
    Literal
};

struct StageResourceBindingModel
{
    QString parameterName;
    BindingValueKind valueKind = BindingValueKind::Unknown;
    QString valueName;
    QString preprocessorCondition;
    QMap<QString, QString> properties;
    SourceLocation location;
};

struct ShaderStageModel
{
    StageKind kind = StageKind::Pixel;
    StageAssignmentKind assignment = StageAssignmentKind::Unset;
    QString genericName;
    QString baseName;
    QString source;
    bool sourceAssigned = false;
    QString entryPoint;
    QStringList defines;
    bool definesAssigned = false;
    QVector<StageResourceBindingModel> resourceBindings;
    SourceLocation location;
};

struct TechniqueModel
{
    QStringList names;
    QString preprocessorCondition;
    QString baseName;
    QString state;
    bool stateAssigned = false;
    QString source;
    bool sourceAssigned = false;
    QStringList defines;
    bool definesAssigned = false;
    QStringList definesAppend;
    ShaderStageModel vertexShader;
    ShaderStageModel pixelShader;
    QMap<QString, QString> properties;
    SourceLocation location;
};

struct TechsetModel
{
    QString sourceName;
    QStringList includes;
    GlobalsModel globals;
    QVector<ParameterModel> parameters;
    QVector<TechniqueModel> techniques;
    QVector<Diagnostic> diagnostics;

    const ParameterModel* findParameter(const QString& name) const;
    const TechniqueModel* findTechnique(const QString& name) const;
};

struct ResolvedShaderStage
{
    StageKind kind = StageKind::Pixel;
    StageAssignmentKind assignment = StageAssignmentKind::Unset;
    QString genericName;
    QString baseName;
    QString source;
    QString entryPoint;
    QStringList defines;
    QVector<StageResourceBindingModel> resourceBindings;
    QStringList inheritanceChain;
};

struct ResolvedTechnique
{
    QString selectedName;
    QStringList aliases;
    QStringList inheritanceChain;
    QString source;
    QStringList defines;
    RenderStateModel renderState;
    ResolvedShaderStage vertexShader;
    ResolvedShaderStage pixelShader;
};

enum class CompiledResourceKind
{
    Unknown,
    Texture,
    Sampler,
    ConstantBuffer,
    StructuredBuffer,
    ByteAddressBuffer,
    UnorderedAccess
};

enum class TextureDimension
{
    Unknown,
    Buffer,
    Texture1D,
    Texture1DArray,
    Texture2D,
    Texture2DArray,
    Texture2DMS,
    Texture2DMSArray,
    Texture3D,
    TextureCube,
    TextureCubeArray
};

enum class NumericKind
{
    Unknown,
    Float,
    UInt,
    SInt
};

struct CompiledResource
{
    QString name;
    CompiledResourceKind kind = CompiledResourceKind::Unknown;
    TextureDimension dimension = TextureDimension::Unknown;
    NumericKind numericKind = NumericKind::Unknown;
    int bindPoint = -1;
    int bindCount = 0;
};

struct CompiledConstantVariable
{
    QString cbufferName;
    QString name;
    int startOffset = 0;
    int size = 0;
    NumericKind numericKind = NumericKind::Unknown;
    int rows = 0;
    int columns = 0;
    int elements = 0;
};

struct CompiledSemantic
{
    QString name;
    int index = 0;
    quint8 mask = 0;
    NumericKind numericKind = NumericKind::Unknown;
    int systemValue = 0;
};

struct CompiledShaderInterface
{
    StageKind stage = StageKind::Pixel;
    QString sourceName;
    QString entryPoint;
    QByteArray bytecode;
    QVector<CompiledResource> resources;
    QVector<CompiledConstantVariable> constants;
    QVector<CompiledSemantic> inputs;
    QVector<CompiledSemantic> outputs;
    int instructionCount = 0;

    const CompiledResource* findResource(const QString& name) const;
};

struct ShaderSourceModel
{
    QString logicalPath;
    QString source;
    QString entryPoint;
};

struct BO3ShaderPackage
{
    QString name;
    QString techsetSource;
    TechsetModel techset;
    PackageConfiguration configuration = PackageConfiguration::Runtime;
    QString selectedTechnique;
    ResolvedTechnique resolvedTechnique;
    QMap<QString, ShaderSourceModel> sources;
    CompiledShaderInterface vertexInterface;
    CompiledShaderInterface pixelInterface;
};

QString toString(PackageConfiguration value);
QString toString(CompatibilityStatus value);
QString toString(DiagnosticLevel value);
QString toString(ParameterKind value);
QString toString(StageKind value);
QString toString(CompiledResourceKind value);
QString toString(TextureDimension value);

RenderStateModel interpretRenderState(const QString& state);

} // namespace bo3
