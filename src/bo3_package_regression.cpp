#include "bo3_package_regression.h"

#include "bo3_package_validation.h"
#include "bo3_package_adapter.h"
#include "bo3_shader_reflection.h"
#include "bo3_techset.h"
#include "bo3_techset_writer.h"
#include "hlsl_preview_mode.h"
#include "live_window_capture.h"
#include "preview_package_session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <functional>

namespace bo3
{
namespace
{

QString fixtureRoot()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("tests/bo3_package"),
        QDir(appDir).filePath("../tests/bo3_package"),
        QDir(appDir).filePath("../../tests/bo3_package"),
        QDir(appDir).filePath("../../../tests/bo3_package")
    };
    for(const QString& candidate : candidates)
        if(QDir(candidate).exists()) return QDir(candidate).absolutePath();
    return {};
}

QString assetRoot(const QString& child)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(child),
        QDir(appDir).filePath("../" + child),
        QDir(appDir).filePath("../../" + child),
        QDir(appDir).filePath("../../../" + child)
    };
    for(const QString& candidate : candidates)
        if(QDir(candidate).exists()) return QDir(candidate).absolutePath();
    return {};
}

QString readText(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

bool hasCode(const ValidationResult& result, const QString& code)
{
    for(const Diagnostic& diagnostic : result.diagnostics)
        if(diagnostic.code == code) return true;
    return false;
}

QString compilePair(const QString& hlsl, CompiledShaderInterface& vertex,
                    CompiledShaderInterface& pixel)
{
    ShaderCompileRequest request;
    request.source = hlsl;
    request.sourceName = "package_fixture.hlsl";
    request.stage = StageKind::Vertex;
    request.entryPoint = "vs_main";
    ShaderCompileResult vs = compileAndReflectShader(request);
    if(!vs.compiled || vs.validation.hasErrors()) return vs.validation.toText();
    request.stage = StageKind::Pixel;
    request.entryPoint = "ps_main";
    ShaderCompileResult ps = compileAndReflectShader(request);
    if(!ps.compiled || ps.validation.hasErrors()) return ps.validation.toText();
    vertex = vs.shaderInterface;
    pixel = ps.shaderInterface;
    return {};
}

BO3ShaderPackage makePackage(const TechsetParseResult& parsed,
                             const TechniqueResolutionResult& resolved,
                             const QString& hlsl,
                             const CompiledShaderInterface& vertex,
                             const CompiledShaderInterface& pixel,
                             PackageConfiguration configuration)
{
    BO3ShaderPackage package;
    package.name = "regression package";
    package.techsetSource = parsed.preprocessedSource;
    package.techset = parsed.model;
    package.configuration = configuration;
    package.selectedTechnique = "lit";
    package.resolvedTechnique = resolved.technique;
    package.sources["package_fixture.hlsl"] = {"package_fixture.hlsl", hlsl, {}};
    package.vertexInterface = vertex;
    package.pixelInterface = pixel;
    return package;
}

QString validateInlineAdapterPackage(const PackageAdapterResult& adapter,
                                     PackageConfiguration configuration)
{
    const QString techsetText = serializeTechset(adapter.techset);
    TechsetParseOptions options;
    options.configuration = configuration;
    const TechsetParseResult parsed = parseTechset(
        techsetText, "temporary_preview.techsetdef", options);
    if(parsed.validation.hasErrors()) return parsed.validation.toText();
    const TechniqueResolutionResult resolved = resolveTechnique(
        parsed.model, adapter.selectedTechnique);
    if(!resolved.found || resolved.validation.hasErrors())
        return resolved.validation.toText();

    CompiledShaderInterface vertex;
    CompiledShaderInterface pixel;
    const QString compileFailure = compilePair(adapter.adaptedSource, vertex, pixel);
    if(!compileFailure.isEmpty()) return compileFailure;

    BO3ShaderPackage package;
    package.name = "temporary adapter package";
    package.techsetSource = parsed.preprocessedSource;
    package.techset = parsed.model;
    package.configuration = configuration;
    package.selectedTechnique = adapter.selectedTechnique;
    package.resolvedTechnique = resolved.technique;
    package.sources[adapter.sourceFileName] = {
        adapter.sourceFileName, adapter.adaptedSource, {}};
    package.vertexInterface = vertex;
    package.pixelInterface = pixel;
    const ValidationResult validation = validatePackage(package);
    return validation.status() == CompatibilityStatus::Pass
        ? QString() : validation.toText();
}

} // namespace

PackageRegressionSummary runPackageRegressionSuite()
{
    PackageRegressionSummary summary;
    auto run = [&](const QString& name, const std::function<QString()>& test)
    {
        const QString failure = test();
        if(failure.isEmpty())
        {
            ++summary.passed;
            summary.output += QString("PASS: %1\n").arg(name);
        }
        else
        {
            ++summary.failed;
            summary.output += QString("FAIL: %1\n%2\n").arg(name, failure);
        }
    };

    const QString root = fixtureRoot();
    if(root.isEmpty())
    {
        summary.failed = 1;
        summary.output = "FAIL: tests/bo3_package fixture directory was not found.\nResult: 0 passed, 1 failed\n";
        return summary;
    }
    const QString techsetText = readText(QDir(root).filePath("basic_structure.techsetdef"));
    const QString hlsl = readText(QDir(root).filePath("package_fixture.hlsl"));
    if(techsetText.isEmpty() || hlsl.isEmpty())
    {
        summary.failed = 1;
        summary.output = "FAIL: BO3 package fixture files could not be read.\nResult: 0 passed, 1 failed\n";
        return summary;
    }

    TechsetParseOptions runtimeOptions;
    runtimeOptions.configuration = PackageConfiguration::Runtime;
    const TechsetParseResult runtime = parseTechset(techsetText, "basic_structure.techsetdef", runtimeOptions);
    TechsetParseOptions toolsOptions;
    toolsOptions.configuration = PackageConfiguration::Toolsgfx;
    const TechsetParseResult tools = parseTechset(techsetText, "basic_structure.techsetdef", toolsOptions);
    const TechniqueResolutionResult runtimeLit = resolveTechnique(runtime.model, "lit");
    const TechniqueResolutionResult toolsUnlit = resolveTechnique(tools.model, "unlit");
    CompiledShaderInterface vertex, pixel;
    const QString compileFailure = compilePair(hlsl, vertex, pixel);

    run("parse corpus-derived techset structure", [&]() -> QString
    {
        if(runtime.validation.hasErrors()) return runtime.validation.toText();
        if(runtime.model.includes != QStringList{"lit_base_shaders"}) return "include was not parsed";
        if(runtime.model.globals.category != "2d") return "Globals category was not parsed";
        if(runtime.model.globals.renderFlags.value("isEmissive") != "true") return "RenderFlags object was not parsed";
        const ParameterModel* tint = runtime.model.findParameter("tint");
        if(!tint || tint->kind != ParameterKind::Float4 || !tint->hasTweak || tint->tweak.title != "Tint")
            return "float4/Tweak metadata was not parsed";
        return {};
    });

    run("technique aliases and inheritance", [&]() -> QString
    {
        if(!runtimeLit.found) return runtimeLit.validation.toText();
        if(!toolsUnlit.found) return toolsUnlit.validation.toText();
        if(!runtimeLit.technique.aliases.contains("lit") || !runtimeLit.technique.aliases.contains("unlit"))
            return "multi-name technique aliases were lost";
        if(runtimeLit.technique.renderState.raw != "replace + nocull") return "base render state was not inherited";
        if(runtimeLit.technique.source != "package_fixture.hlsl") return "technique source was not inherited";
        if(runtimeLit.technique.defines != QStringList{"BASE", "CHILD"}) return "defines replacement/append order is wrong";
        return {};
    });

    run("structured techset serialization round trip", [&]() -> QString
    {
        const QString serialized = serializeTechset(runtime.model);
        const TechsetParseResult roundTrip = parseTechset(serialized, "round_trip.techsetdef", runtimeOptions);
        if(roundTrip.validation.hasErrors()) return roundTrip.validation.toText();
        const TechniqueResolutionResult technique = resolveTechnique(roundTrip.model, "lit");
        if(!technique.found) return technique.validation.toText();
        if(technique.technique.source != "package_fixture.hlsl" ||
           technique.technique.renderState.raw != "replace + nocull" ||
           technique.technique.pixelShader.resourceBindings.value(0).valueKind != BindingValueKind::CodeTexture)
            return "serialized model did not preserve resolved package fields";
        return {};
    });

    run("serializer quotes sampler filter display strings", [&]() -> QString
    {
        TechsetModel model;
        model.sourceName = "sampler_filter_quote.techsetdef";
        model.globals.category = "2d";

        ParameterModel sampler;
        sampler.kind = ParameterKind::Sampler;
        sampler.name = "bilinearClampler";
        sampler.properties["tile"] = "no tile";
        sampler.properties["filter"] = "linear (mip none)";
        model.parameters << sampler;

        const QString serialized = serializeTechset(model);
        if(!serialized.contains("filter = \"linear (mip none)\""))
            return "sampler filter value was emitted as an unquoted expression";

        const TechsetParseResult parsed = parseTechset(
            serialized, "sampler_filter_quote.techsetdef", runtimeOptions);
        if(parsed.validation.hasErrors()) return parsed.validation.toText();
        const ParameterModel* reparsed = parsed.model.findParameter("bilinearClampler");
        if(!reparsed || reparsed->properties.value("filter") != "linear (mip none)")
            return "quoted sampler filter value did not survive round trip";

        const QString invalid =
            "Globals()\n{\n    category = \"2d\"\n}\n\n"
            "Sampler(\"bilinearClampler\")\n{\n"
            "    filter = linear (mip none)\n"
            "    tile = \"no tile\"\n}\n";
        const TechsetParseResult invalidParsed = parseTechset(
            invalid, "invalid_sampler_filter.techsetdef", runtimeOptions);
        if(!invalidParsed.validation.hasErrors())
            return "BO3-invalid unquoted sampler filter was not rejected";
        return {};
    });

    run("structured material package generation", [&]() -> QString
    {
        TechsetModel material;
        material.sourceName = "generated_material.techsetdef";
        material.includes << "lit_base_shaders";
        material.globals.category = "Geometry Custom";
        material.globals.renderFlags["isEmissive"] = "true";

        ParameterModel sampler;
        sampler.kind = ParameterKind::Sampler;
        sampler.name = "bilinearClampler";
        sampler.properties["tile"] = "tile both";
        sampler.properties["filter"] = "linear (mip linear)";
        material.parameters << sampler;

        ParameterModel texture;
        texture.kind = ParameterKind::Texture;
        texture.name = "frameBuffer";
        texture.properties["image"] = "Image(<colorMap00, $white_diffuse>)";
        texture.properties["semantic"] = "diffuseMap";
        texture.hasTweak = true;
        texture.tweak.category = "Color";
        texture.tweak.title = "Frame Buffer";
        texture.tweak.order = "0";
        material.parameters << texture;

        ParameterModel tint;
        tint.kind = ParameterKind::Float4;
        tint.name = "tint";
        tint.properties["x"] = "<cg0_x>";
        tint.properties["y"] = "<cg0_y>";
        tint.properties["z"] = "<cg0_z>";
        tint.properties["w"] = "<cg0_w>";
        material.parameters << tint;

        TechniqueModel technique;
        technique.names << "lit";
        technique.state = "blend + depth";
        technique.stateAssigned = true;
        technique.source = "package_fixture.hlsl";
        technique.sourceAssigned = true;
        technique.vertexShader.kind = StageKind::Vertex;
        technique.vertexShader.assignment = StageAssignmentKind::GenericName;
        technique.vertexShader.genericName = "vs_generic";
        technique.pixelShader.kind = StageKind::Pixel;
        technique.pixelShader.assignment = StageAssignmentKind::GenericName;
        technique.pixelShader.genericName = "ps_generic";
        material.techniques << technique;

        const QString serialized = serializeTechset(material);
        const TechsetParseResult parsed = parseTechset(serialized, material.sourceName, runtimeOptions);
        if(parsed.validation.hasErrors()) return parsed.validation.toText();
        const TechniqueResolutionResult resolved = resolveTechnique(parsed.model, "lit");
        if(!resolved.found) return resolved.validation.toText();
        const ParameterModel* reflectedTexture = parsed.model.findParameter("frameBuffer");
        if(!reflectedTexture || !reflectedTexture->hasTweak ||
           reflectedTexture->tweak.title != "Frame Buffer")
            return "structured Texture/Tweak material interface did not round-trip";
        if(!compileFailure.isEmpty()) return compileFailure;
        const ValidationResult validation = validatePackage(
            makePackage(parsed, resolved, hlsl, vertex, pixel, PackageConfiguration::Runtime));
        return validation.status() == CompatibilityStatus::Pass ? QString() : validation.toText();
    });

    run("corpus basic structure", [&]() -> QString
    {
        TechsetParseOptions options = runtimeOptions;
        options.defines["TETRAHEDRON_OMNI_SHADOWS"] = "1";
        const TechsetParseResult parsed = parseTechset(
            readText(QDir(root).filePath("corpus_basic.techsetdef")), "corpus_basic.techsetdef", options);
        if(parsed.validation.hasErrors()) return parsed.validation.toText();
        const TechniqueResolutionResult motion = resolveTechnique(parsed.model, "gbuffer motion vector");
        if(!motion.found) return motion.validation.toText();
        if(parsed.model.globals.category != "Geometry" || parsed.model.techniques.size() != 4)
            return "basic Globals/conditional techniques were not preserved";
        if(motion.technique.pixelShader.source != "gbuffer_lit_ps_semi-reversed.hlsl" ||
           !motion.technique.defines.contains("GENERATE_MOTION_VECTOR"))
            return "basic technique source/defines inheritance failed";
        return {};
    });

    run("corpus decal structure", [&]() -> QString
    {
        TechsetParseOptions options = runtimeOptions;
        options.defines["MTL_TYPE_VOL_DECAL"] = "1";
        const TechsetParseResult parsed = parseTechset(
            readText(QDir(root).filePath("corpus_decal_emissive_reveal.techsetdef")),
            "corpus_decal_emissive_reveal.techsetdef", options);
        if(parsed.validation.hasErrors()) return parsed.validation.toText();
        const ParameterModel* alias = parsed.model.findParameter("colorMap");
        const ParameterModel* tweak = parsed.model.findParameter("rotateUVs");
        const TechniqueResolutionResult lit = resolveTechnique(parsed.model, "lit");
        if(!alias || alias->baseName != "emissiveMap") return "decal Texture alias was not parsed";
        if(!tweak || !tweak->hasTweak || tweak->tweak.category != "Mask Motion") return "decal inline Tweak was not parsed";
        if(!lit.found || lit.technique.pixelShader.source != "decal_emissive_reveal_ps.hlsl")
            return "decal PS source override was not resolved";
        return {};
    });

    run("corpus End Portal runtime and TOOLSGFX", [&]() -> QString
    {
        const QString source = readText(QDir(root).filePath("corpus_endportal.techsetdef"));
        const TechsetParseResult runtimeParsed = parseTechset(source, "corpus_endportal.techsetdef", runtimeOptions);
        const TechsetParseResult toolsParsed = parseTechset(source, "corpus_endportal.techsetdef", toolsOptions);
        if(runtimeParsed.validation.hasErrors()) return runtimeParsed.validation.toText();
        if(toolsParsed.validation.hasErrors()) return toolsParsed.validation.toText();
        const TechniqueResolutionResult runtimeTechnique = resolveTechnique(runtimeParsed.model, "lit");
        const TechniqueResolutionResult toolsTechnique = resolveTechnique(toolsParsed.model, "lit");
        if(!runtimeTechnique.found || !toolsTechnique.found)
        {
            QStringList runtimeNames, toolsNames, runtimeParameters, toolsParameters;
            for(const TechniqueModel& technique : runtimeParsed.model.techniques) runtimeNames += technique.names;
            for(const TechniqueModel& technique : toolsParsed.model.techniques) toolsNames += technique.names;
            for(const ParameterModel& parameter : runtimeParsed.model.parameters) runtimeParameters << parameter.name;
            for(const ParameterModel& parameter : toolsParsed.model.parameters) toolsParameters << parameter.name;
            return QString("End Portal lit technique was not resolved (runtime techniques: %1; TOOLSGFX techniques: %2; runtime parameters: %3; TOOLSGFX parameters: %4)\n%5\n%6")
                .arg(runtimeNames.join(", "), toolsNames.join(", "), runtimeParameters.join(", "),
                     toolsParameters.join(", "), runtimeTechnique.validation.toText(), toolsTechnique.validation.toText());
        }
        if(runtimeTechnique.technique.pixelShader.source != "endportal_ps.hlsl") return "runtime PS branch is wrong";
        if(toolsTechnique.technique.pixelShader.source != "specialty/emissive_objective.hlsl") return "TOOLSGFX PS branch is wrong";
        if(runtimeParsed.model.findParameter("colorMap")) return "runtime incorrectly retained TOOLSGFX fallback parameter";
        if(!toolsParsed.model.findParameter("colorMap")) return "TOOLSGFX fallback parameter is missing";
        return {};
    });

    run("Aurora sky runtime and TOOLSGFX package selection", [&]() -> QString
    {
        const QString templates = assetRoot("export_templates");
        if(templates.isEmpty()) return "export_templates directory was not found";
        const QString runtimeText = readText(QDir(templates).filePath("sky_aurora_runtime.techsetdef"));
        const QString toolsText = readText(QDir(templates).filePath("sky_aurora_toolsgfx.techsetdef"));
        if(runtimeText.isEmpty() || toolsText.isEmpty()) return "Aurora sky templates could not be read";
        const TechsetParseResult runtimeSky = parseTechset(runtimeText, "sky_runtime.techsetdef", runtimeOptions);
        const TechsetParseResult toolsSky = parseTechset(toolsText, "sky_toolsgfx.techsetdef", toolsOptions);
        if(runtimeSky.validation.hasErrors()) return runtimeSky.validation.toText();
        if(toolsSky.validation.hasErrors()) return toolsSky.validation.toText();
        const TechniqueResolutionResult runtimeLitSky = resolveTechnique(runtimeSky.model, "lit");
        const TechniqueResolutionResult toolsLitSky = resolveTechnique(toolsSky.model, "lit");
        if(!runtimeLitSky.found || !toolsLitSky.found)
            return runtimeLitSky.validation.toText() + "\n" + toolsLitSky.validation.toText();
        if(runtimeLitSky.technique.pixelShader.source !=
           "geometry/sky_procedural_aurora_borealis_ps.hlsl")
            return "runtime sky did not select its custom procedural PS";
        if(toolsLitSky.technique.pixelShader.source != "techsetdef_sky_latlong_hdr.hlsl")
            return "TOOLSGFX sky did not retain the stock ps_sky source";
        if(runtimeSky.model.findParameter("colorMap") == nullptr ||
           toolsSky.model.findParameter("colorMap") == nullptr)
            return "sky material Texture parameter was not parsed in both configurations";
        return {};
    });

    run("runtime CodeTexture branch", [&]() -> QString
    {
        if(!runtimeLit.found) return runtimeLit.validation.toText();
        const auto& bindings = runtimeLit.technique.pixelShader.resourceBindings;
        if(bindings.size() != 1 || bindings[0].valueKind != BindingValueKind::CodeTexture ||
           bindings[0].valueName != "resolvedScene") return "runtime branch did not resolve resolvedScene CodeTexture";
        return {};
    });

    run("TOOLSGFX texture fallback branch", [&]() -> QString
    {
        if(!toolsUnlit.found) return toolsUnlit.validation.toText();
        const auto& bindings = toolsUnlit.technique.pixelShader.resourceBindings;
        if(bindings.size() != 1 || bindings[0].valueKind != BindingValueKind::Texture)
            return "TOOLSGFX branch did not resolve Texture fallback";
        if(!bindings[0].properties.contains("image")) return "TOOLSGFX Texture fallback lost its Image property";
        return {};
    });

    run("optimized VS and PS reflection", [&]() -> QString
    {
        if(!compileFailure.isEmpty()) return compileFailure;
        if(!pixel.findResource("frameBuffer") || !pixel.findResource("bilinearClampler"))
            return "optimized PS resource reflection is incomplete";
        bool hasTint = false;
        for(const auto& constant : pixel.constants) if(constant.name == "tint") hasTint = true;
        if(!hasTint) return "optimized cbuffer variable reflection is incomplete";
        if(vertex.outputs.size() < 2 || pixel.inputs.size() < 2) return "signature reflection is incomplete";
        return {};
    });

    run("valid runtime package", [&]() -> QString
    {
        if(!compileFailure.isEmpty()) return compileFailure;
        BO3ShaderPackage package = makePackage(runtime, runtimeLit, hlsl, vertex, pixel, PackageConfiguration::Runtime);
        const ValidationResult result = validatePackage(package);
        if(result.status() != CompatibilityStatus::Pass) return result.toText();
        return {};
    });

    run("valid TOOLSGFX package", [&]() -> QString
    {
        if(!compileFailure.isEmpty()) return compileFailure;
        BO3ShaderPackage package = makePackage(tools, toolsUnlit, hlsl, vertex, pixel, PackageConfiguration::Toolsgfx);
        const ValidationResult result = validatePackage(package);
        if(result.status() != CompatibilityStatus::Pass) return result.toText();
        return {};
    });

    run("unbundled stock stage remains unknown", [&]() -> QString
    {
        if(!compileFailure.isEmpty()) return compileFailure;
        BO3ShaderPackage package = makePackage(runtime, runtimeLit, hlsl, vertex, pixel,
                                               PackageConfiguration::Runtime);
        package.resolvedTechnique.vertexShader.source = "stock/postfx_fullscreen.hlsl";
        package.vertexInterface = {};
        const ValidationResult result = validatePackage(package);
        if(result.hasErrors()) return result.toText();
        if(result.status() != CompatibilityStatus::Unknown ||
           !hasCode(result, "PACKAGE_EXTERNAL_STAGE_SOURCE"))
            return "missing stock bytecode was incorrectly treated as proven-compatible";
        return {};
    });

    run("invalid sampler binding", [&]() -> QString
    {
        TechsetModel broken = runtime.model;
        for(ParameterModel& parameter : broken.parameters)
            if(parameter.name == "bilinearClampler") parameter.name = "wrongSampler";
        const ValidationResult result = validateStageResources(broken, runtimeLit.technique.pixelShader, pixel,
                                                               defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_MISSING_SAMPLER") ? QString() : result.toText();
    });

    run("missing Texture binding", [&]() -> QString
    {
        TechsetModel broken = runtime.model;
        for(int i = broken.parameters.size() - 1; i >= 0; --i)
            if(broken.parameters[i].name == "frameBuffer") broken.parameters.removeAt(i);
        ResolvedShaderStage stage = runtimeLit.technique.pixelShader;
        stage.resourceBindings.clear();
        const ValidationResult result = validateStageResources(broken, stage, pixel, defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_MISSING_TEXTURE") ? QString() : result.toText();
    });

    run("CodeTexture resource type mismatch", [&]() -> QString
    {
        QString cubeHlsl = hlsl;
        cubeHlsl.replace("Texture2D<float4> frameBuffer", "TextureCube<float4> frameBuffer");
        cubeHlsl.replace("frameBuffer.Sample(bilinearClampler, input.uv)",
                         "frameBuffer.Sample(bilinearClampler, float3(input.uv, 1.0))");
        CompiledShaderInterface cubeVertex, cubePixel;
        const QString problem = compilePair(cubeHlsl, cubeVertex, cubePixel);
        if(!problem.isEmpty()) return problem;
        const ValidationResult result = validateStageResources(runtime.model, runtimeLit.technique.pixelShader,
                                                               cubePixel, defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_CODE_TEXTURE_TYPE") ? QString() : result.toText();
    });

    run("Texture parameter resource type mismatch", [&]() -> QString
    {
        QString cubeHlsl = hlsl;
        cubeHlsl.replace("Texture2D<float4> frameBuffer", "TextureCube<float4> frameBuffer");
        cubeHlsl.replace("frameBuffer.Sample(bilinearClampler, input.uv)",
                         "frameBuffer.Sample(bilinearClampler, float3(input.uv, 1.0))");
        CompiledShaderInterface cubeVertex, cubePixel;
        const QString problem = compilePair(cubeHlsl, cubeVertex, cubePixel);
        if(!problem.isEmpty()) return problem;
        ResolvedShaderStage stage = runtimeLit.technique.pixelShader;
        stage.resourceBindings.clear();
        const ValidationResult result = validateStageResources(runtime.model, stage, cubePixel,
                                                               defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_TEXTURE_TYPE") ? QString() : result.toText();
    });

    run("optimized-out explicit binding", [&]() -> QString
    {
        QString strippedHlsl = hlsl;
        strippedHlsl.replace("return frameBuffer.Sample(bilinearClampler, input.uv) * tint;",
                             "return tint;");
        CompiledShaderInterface strippedVertex, strippedPixel;
        const QString problem = compilePair(strippedHlsl, strippedVertex, strippedPixel);
        if(!problem.isEmpty()) return problem;
        const ValidationResult result = validateStageResources(runtime.model, runtimeLit.technique.pixelShader,
                                                               strippedPixel, defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_BINDING_OPTIMIZED_OUT") ? QString() : result.toText();
    });

    run("optimized-out top-level parameter warning", [&]() -> QString
    {
        QString strippedHlsl = hlsl;
        strippedHlsl.replace("return frameBuffer.Sample(bilinearClampler, input.uv) * tint;",
                             "return tint;");
        CompiledShaderInterface strippedVertex, strippedPixel;
        const QString problem = compilePair(strippedHlsl, strippedVertex, strippedPixel);
        if(!problem.isEmpty()) return problem;
        TechniqueResolutionResult withoutBinding = runtimeLit;
        withoutBinding.technique.pixelShader.resourceBindings.clear();
        BO3ShaderPackage package = makePackage(runtime, withoutBinding, strippedHlsl,
                                               strippedVertex, strippedPixel,
                                               PackageConfiguration::Runtime);
        const ValidationResult result = validatePackage(package);
        if(result.hasErrors()) return result.toText();
        return hasCode(result, "PACKAGE_UNUSED_PARAMETER") ? QString() : result.toText();
    });

    run("missing material constant", [&]() -> QString
    {
        TechsetModel broken = runtime.model;
        for(int i = broken.parameters.size() - 1; i >= 0; --i)
            if(broken.parameters[i].name == "tint") broken.parameters.removeAt(i);
        const ValidationResult result = validateStageResources(broken, runtimeLit.technique.pixelShader, pixel,
                                                               defaultPackageValidationOptions());
        return hasCode(result, "PACKAGE_MISSING_CONSTANT") ? QString() : result.toText();
    });

    run("VS/PS semantic match", [&]() -> QString
    {
        if(!compileFailure.isEmpty()) return compileFailure;
        const ValidationResult result = validateShaderInterfacePair(vertex, pixel);
        return result.hasErrors() ? result.toText() : QString();
    });

    run("VS/PS semantic mismatch", [&]() -> QString
    {
        CompiledShaderInterface broken = vertex;
        for(int i = broken.outputs.size() - 1; i >= 0; --i)
            if(broken.outputs[i].name == "TEXCOORD" && broken.outputs[i].index == 0) broken.outputs.removeAt(i);
        const ValidationResult result = validateShaderInterfacePair(broken, pixel);
        return hasCode(result, "PACKAGE_SEMANTIC_MISSING") ? QString() : result.toText();
    });

    run("render-state interpretation", [&]() -> QString
    {
        const RenderStateModel state = interpretRenderState("add + depth + decal");
        if(state.blend != RenderBlendMode::Add || !state.depth || !state.decal || !state.unknownTokens.isEmpty())
            return "known render-state tokens were not interpreted";
        const RenderStateModel sky = interpretRenderState("sky");
        if(!sky.sky || !sky.unknownTokens.isEmpty())
            return "corpus-proven sky render state was treated as unknown";
        const RenderStateModel unknown = interpretRenderState("replace + mystery");
        return unknown.unknownTokens == QStringList{"mystery"} ? QString() : "unknown render-state token was hidden";
    });

    run("invalid stock include path", [&]() -> QString
    {
        const ValidationResult result = validateSourcePath("_custom/effect.hlsl", "#include \"postfx/postfx_common.h\"\n");
        return hasCode(result, "PACKAGE_STOCK_INCLUDE_NESTED_PATH") ? QString() : result.toText();
    });

    run("valid root stock include path", [&]() -> QString
    {
        const ValidationResult result = validateSourcePath("effect.hlsl", "#include \"postfx/postfx_common.h\"\n");
        return result.hasErrors() ? result.toText() : QString();
    });

    run("generic HLSL preview policy", [&]() -> QString
    {
        const PreviewPolicy generic = previewPolicy(ShaderPreviewMode::Hlsl);
        if(generic.enforcePackage || !generic.allowSyntheticGlobals ||
           !generic.previewWithoutPackage || generic.packageStatus != "N/A")
            return "generic HLSL mode incorrectly inherited BO3 package enforcement";
        for(const ShaderPreviewMode strict : {ShaderPreviewMode::PostFx,
                                               ShaderPreviewMode::Material,
                                               ShaderPreviewMode::Skybox})
        {
            const PreviewPolicy policy = previewPolicy(strict);
            if(!policy.enforcePackage || policy.allowSyntheticGlobals || policy.previewWithoutPackage)
                return QString("strict %1 policy was weakened").arg(toString(strict));
        }
        return {};
    });

    run("BO3 Runtime default Scene EV", [&]() -> QString
    {
        if(std::abs(kDefaultBo3RuntimeSceneExposureEv) > 0.0001f)
            return "runtime Scene EV default is not the neutral 0.00 EV";
        const float scale = std::exp2(kDefaultBo3RuntimeSceneExposureEv);
        return std::abs(scale - 1.0f) < 0.0001f ? QString()
            : "runtime Scene EV default is not an identity 2^EV scale";
    });

    run("same-basename package association has no stale reuse", [&]() -> QString
    {
        QTemporaryDir temporary;
        if(!temporary.isValid()) return "could not create temporary association fixture";
        const QString shaderA = QDir(temporary.path()).filePath("shader_a.hlsl");
        const QString techsetA = QDir(temporary.path()).filePath("shader_a.techsetdef");
        const QString shaderB = QDir(temporary.path()).filePath("shader_b.hlsl");
        const QString techsetB = QDir(temporary.path()).filePath("shader_b.techsetdef");
        for(const QString& path : {shaderA, techsetA, shaderB})
        {
            QFile file(path);
            if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) return "could not write association fixture";
            file.write("fixture");
        }
        if(adjacentTechsetPath(shaderA) != QFileInfo(techsetA).absoluteFilePath())
            return "shader A did not select techset A";
        if(!adjacentTechsetPath(shaderB).isEmpty())
            return "shader B reused shader A's techset";
        QFile fileB(techsetB);
        if(!fileB.open(QIODevice::WriteOnly | QIODevice::Text)) return "could not write techset B";
        fileB.write("fixture"); fileB.close();
        return adjacentTechsetPath(shaderB) == QFileInfo(techsetB).absoluteFilePath()
            ? QString() : "shader B did not select techset B";
    });

    const QString autoPostFxSource = R"(
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);
SamplerState frameBufferSampler : register(s0);
SamplerState DepthSamplerState : register(s1);
struct PS_INPUT { float4 position : SV_Position; float2 texcoord : TEXCOORD0; };
float4 ps_main(PS_INPUT input) : SV_Target
{
    return frameBuffer.Sample(frameBufferSampler, input.texcoord) +
           DepthSampler.Sample(DepthSamplerState, input.texcoord).rrrr;
}
)";

    const QString materialPreviewSource = R"(
Texture2D<float4> diffuse : register(t0);
SamplerState colorSampler : register(s0);
float materialStrength = 1.0;
float bo3MaterialOutputScale = 1.0;
struct MaterialVSInput { float3 position:POSITION; float2 texcoord:TEXCOORD0; };
struct MaterialPSInput { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
MaterialPSInput vs_main(MaterialVSInput v)
{
    MaterialPSInput o; o.position=float4(v.position,1); o.texcoord=v.texcoord; return o;
}
float4 ps_main(MaterialPSInput p):SV_Target
{
    return diffuse.Sample(colorSampler,p.texcoord) * materialStrength;
}
)";

    const QString skyPreviewSource = R"(
TextureCube<float4> colorMap : register(t0);
SamplerState colorSampler : register(s0);
struct SkyVSInput { float3 position:POSITION; };
struct SkyInput { float4 position:SV_Position; float3 skyDirection:TEXCOORD0; };
SkyInput vs_main(SkyVSInput v)
{
    SkyInput o; o.position=float4(v.position,1); o.skyDirection=v.position; return o;
}
float4 ps_main(SkyInput p):SV_Target
{
    return colorMap.Sample(colorSampler,normalize(p.skyDirection));
}
)";

    run("temporary generic HLSL preview is available without BO3 proof", [&]() -> QString
    {
        const QString source =
            "float4 ps_main(float4 position:SV_Position):SV_Target{return float4(1,0,0,1);}";
        ShaderCompileRequest compileRequest;
        compileRequest.source = source;
        compileRequest.sourceName = "standalone.hlsl";
        compileRequest.stage = StageKind::Pixel;
        compileRequest.entryPoint = "ps_main";
        const ShaderCompileResult compile = compileAndReflectShader(compileRequest);
        if(!compile.compiled || compile.validation.hasErrors())
            return compile.validation.toText();

        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::Hlsl;
        request.source = source;
        request.sourceFileName = "standalone.hlsl";
        const PreviewPackageState state = session.rebuild(request);
        const PreviewPolicy policy = previewPolicy(ShaderPreviewMode::Hlsl);
        if(state.origin != PreviewPackageOrigin::TemporaryGeneric ||
           !state.readyForPreviewCompilation || state.packageValidationRequired ||
           !state.temporaryTechsetText.isEmpty() || state.canPersist() ||
           policy.packageStatus != "N/A")
            return "generic preview was mistaken for a persistent or BO3-validated package";
        request.entryPoint = "alternate_entry";
        const PreviewPackageState changedEntry = session.rebuild(request);
        if(changedEntry.generation <= state.generation ||
           changedEntry.stateFingerprint == state.stateFingerprint)
            return "entry-point change did not invalidate the temporary preview harness";
        return {};
    });

    run("commented shader resources never become package parameters", [&]() -> QString
    {
        const QString source = R"(
// Texture2D<float4> frameBuffer : register(t0);
/* SamplerState glslSampler : register(s1); */
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler0 : register(s2);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    return iChannel0.Sample(glslSampler0, input.texcoord);
}
)";
        const ShaderSourceAnalysis analysis = analyzeShaderSource(source);
        auto contains = [](const QVector<SourceResource>& resources, const QString& name)
        {
            for(const SourceResource& resource : resources)
                if(resource.name.compare(name, Qt::CaseInsensitive) == 0) return true;
            return false;
        };
        if(contains(analysis.textures, "frameBuffer") || contains(analysis.samplers, "glslSampler"))
            return "commented-out resources leaked into source analysis";
        if(!contains(analysis.textures, "iChannel0") || !contains(analysis.samplers, "glslSampler0"))
            return "live resources were lost while filtering comments";
        return {};
    });

    run("converted Shadertoy buffer dependency is not faked into single-pass PostFX", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Image
//   iChannel0 <- Buffer A | Used -> Preserve Inputs
// Buffer references are project metadata in this preview; runtime multipass routing is the next phase.
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler0 : register(s2);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    return iChannel0.Sample(glslSampler0, input.texcoord);
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "converted_image.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        return result.confidence == AutomationConfidence::Unsupported &&
               hasCode(result.diagnostics, "ADAPTER_POSTFX_MULTIPASS_DEPENDENCY")
            ? QString() : "Buffer A dependency was silently replaced by a single-pass PostFX binding";
    });

    run("converted Shadertoy PostFX gets safe automatic channel mappings", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
Texture2D<float4> iChannel0 : register(t2);
Texture2D<float4> iChannel1 : register(t3);
SamplerState glslSampler0 : register(s2);
SamplerState glslSampler1 : register(s3);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float4 fragColor = iChannel0.Sample(glslSampler0, input.texcoord) +
                       iChannel1.Sample(glslSampler1, input.texcoord) * 0.01;
    return fragColor;
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "converted_buffer_a.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();

        auto mapping = [&](const QString& name) -> const PackageResourceMapping*
        {
            for(const PackageResourceMapping& candidate : result.mappings)
                if(candidate.resourceName.compare(name, Qt::CaseInsensitive) == 0) return &candidate;
            return nullptr;
        };
        const PackageResourceMapping* channel0 = mapping("iChannel0");
        const PackageResourceMapping* channel1 = mapping("iChannel1");
        const PackageResourceMapping* sampler = mapping("bilinearClampler");
        if(!channel0 || channel0->role != PackageResourceRole::ResolvedScene ||
           channel0->selectedBinding != "resolvedScene")
            return "converted iChannel0 was not automatically mapped to resolvedScene";
        if(!channel1 || channel1->role != PackageResourceRole::MaterialImage ||
           channel1->selectedBinding != "materialImage")
            return "converted auxiliary channel was not preserved as a material image";
        if(!sampler || sampler->selectedBinding != "keep" ||
           result.adaptedSource.contains(QRegularExpression(R"(\bglslSampler[0-3]*\b)",
               QRegularExpression::CaseInsensitiveOption)))
            return "converter-generated samplers were not collapsed to a proven BO3 sampler";
        if(!result.adaptedSource.contains("BO3_SCENE_SAMPLE(iChannel0,") ||
           result.adaptedSource.contains("BO3_SCENE_SAMPLE(iChannel1,") ||
           !result.adaptedSource.contains("PostFx_DenormalizeColor(fragColor.rgb)"))
            return "converted PostFX did not bridge the resolvedScene Runtime color domain without altering auxiliary material-image channels";
        return {};
    });

    run("converted Shadertoy macro samples normalize only resolvedScene channels", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
Texture2D<float4> iChannel0 : register(t2);
Texture2D<float4> iChannel1 : register(t3);
SamplerState glslSampler0 : register(s2);
SamplerState glslSampler1 : register(s3);
#define GLSL_TEXTURE_LEVEL_S(tex, samp, uv, lod) tex.SampleLevel(samp, (uv), (lod))
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float4 fragColor = GLSL_TEXTURE_LEVEL_S(iChannel0, glslSampler0, input.texcoord, 0.0) +
                       GLSL_TEXTURE_LEVEL_S(iChannel1, glslSampler1, input.texcoord, 0.0) * 0.01;
    return fragColor;
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "converted_macro_buffer.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(!result.adaptedSource.contains("BO3_SCENE_TEXTURE_LEVEL_S(iChannel0,"))
            return "resolvedScene GLSL_TEXTURE_LEVEL_S sample was not normalized";
        if(result.adaptedSource.contains("BO3_SCENE_TEXTURE_LEVEL_S(iChannel1,"))
            return "material-image GLSL_TEXTURE_LEVEL_S sample was incorrectly normalized";
        if(!result.adaptedSource.contains("GLSL_TEXTURE_LEVEL_S(iChannel1,"))
            return "material-image sample was not preserved";
        return {};
    });

    run("shader camera movement removal preserves unrelated time animation", [&]() -> QString
    {
        const QString source = R"(
float4 mainImage(float2 fragCoord)
{
    float2 pos = fragCoord.xy;
    pos += 4.0*sin(iTime*.5*float2(1,1.7))*iResolution.y/400.;
    float shimmer = 0.5 + 0.5*sin(iTime * 3.0);
    return float4(pos.x / iResolution.x, shimmer, 0.0, 1.0);
}
)";
        const ShaderMotionTransformResult transformed =
            removeShaderCameraInputMovement(source);
        if(!transformed.changed)
            return "recognized fullscreen time-driven drift was not removed";
        if(transformed.source.contains("pos += 4.0*sin(iTime"))
            return "time-driven fullscreen position update survived movement removal";
        if(!transformed.source.contains("float shimmer = 0.5 + 0.5*sin(iTime * 3.0);"))
            return "unrelated time animation was incorrectly removed";
        if(source.contains("BO3_PREVIEWER_REMOVED_CAMERA_INPUT_MOVEMENT"))
            return "motion transform mutated the original source string";
        return {};
    });

    run("self-camera sky bypasses generic camera movement removal", [&]() -> QString
    {
        const QString source = R"(
// BO3_PREVIEWER_SKY_SELF_CAMERA
static float3 BO3_SKY_DIRECTION = float3(0,1,0);
float3 BO3_ShaderToySkyDirection(){ return BO3_SKY_DIRECTION; }
float4 mainImage(float2 fragCoord)
{
    float3 rd = BO3_ShaderToySkyDirection();
    float3 cameraNoise = float3(sin(iTime), iMouse.x, 0.0);
    return float4(rd + cameraNoise * 0.0, 1.0);
}
)";
        const ShaderMotionTransformResult transformed =
            removeShaderCameraInputMovement(source);
        if(transformed.changed)
            return "generic camera/input removal rewrote an already-adapted self-camera sky";
        if(transformed.source != source)
            return "self-camera sky source changed even though the adapter marker should make it authoritative";
        bool foundSkipDiagnostic = false;
        for(const Diagnostic& diagnostic : transformed.diagnostics.diagnostics)
            foundSkipDiagnostic = foundSkipDiagnostic ||
                diagnostic.code == "ADAPTER_SHADER_CAMERA_INPUT_MOVEMENT_ALREADY_REPLACED";
        if(!foundSkipDiagnostic)
            return "self-camera sky skip did not report its protection diagnostic";
        return {};
    });

    run("temporary package persists the transformed movement-free source", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler0 : register(s2);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float2 fragCoord = input.position.xy;
    float2 pos = fragCoord.xy;
    pos += sin(iTime) * float2(4.0, 2.0);
    float shimmer = sin(iTime * 5.0) * 0.01;
    return iChannel0.Sample(glslSampler0, pos / float2(1920.0, 1080.0)) + shimmer;
}
)";
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.configuration = PackageConfiguration::Runtime;
        request.source = source;
        request.sourceFileName = "movement_free_preview.hlsl";
        request.removeShaderCameraInputMovement = true;
        const PreviewPackageState& state = session.rebuild(request);
        if(state.confidence != AutomationConfidence::Auto)
            return state.adapter.diagnostics.toText();
        if(state.source != source)
            return "temporary motion removal changed the stored authored source";
        if(state.adaptedSource.contains("pos += sin(iTime)"))
            return "temporary adapted source still contains recognized camera/screen drift";
        if(!state.adaptedSource.contains("float shimmer = sin(iTime * 5.0) * 0.01;"))
            return "temporary adapted source removed unrelated shader animation";
        const PackageAdapterResult saved = session.persistentResult("saved_movement_free.hlsl");
        if(saved.adaptedSource.isEmpty() || saved.adaptedSource.contains("pos += sin(iTime)"))
            return "persistent package did not serialize the exact movement-free adapted source";
        return {};
    });

    run("resolvedScene channels use fullscreen dimensions and Shadertoy-correct scene orientation", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
Texture2D<float4> iChannel0 : register(t2);
Texture2D<float4> iChannel1 : register(t3);
SamplerState glslSampler0 : register(s2);
SamplerState glslSampler1 : register(s3);
#define iResolution (float3(PostFx_GetRenderTargetSize().xy, 1.0))
int2 GLSL_TEXTURE_SIZE(Texture2D<float4> tex, int lod) { uint w=1,h=1,l=1; tex.GetDimensions((uint)lod,w,h,l); return int2(w,h); }
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    float2 sourceSize = float2(GLSL_TEXTURE_SIZE(iChannel0, 0));
    float2 noiseSize = float2(GLSL_TEXTURE_SIZE(iChannel1, 0));
    float2 uv = (fragCoord - 0.5*iResolution.xy) * min(sourceSize.y/iResolution.y, sourceSize.x/iResolution.x) / sourceSize + 0.5;
    return iChannel0.Sample(glslSampler0, uv) + iChannel1.Sample(glslSampler1, fragCoord/noiseSize) * 0.01;
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "fullscreen_size.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(!result.adaptedSource.contains("float2 sourceSize = float2(int2(iResolution.xy));"))
            return "resolvedScene textureSize was not tied to the PostFX render-target dimensions";
        if(!result.adaptedSource.contains("float2 noiseSize = float2(GLSL_TEXTURE_SIZE(iChannel1, 0));"))
            return "auxiliary material-image dimensions were incorrectly replaced by render-target size";
        if(result.adaptedSource.count("iResolution.y - input.position.y") != 1)
            return "PostFX adaptation introduced or removed the converted Shadertoy fragCoord flip";
        if(!result.adaptedSource.contains("BO3_PostFxSceneUV(uv)"))
            return "resolvedScene sampling did not normalize Shadertoy lower-left UVs to BO3/D3D upper-left texture orientation";
        if(result.adaptedSource.contains("BO3_PostFxSceneUV(fragCoord/noiseSize)"))
            return "auxiliary material-image sampling was incorrectly given the resolvedScene orientation bridge";
        return {};
    });

    run("converted PostFX can preserve an authored upper-left scene orientation", [&]() -> QString
    {
        const QString source = R"(
// BO3_PREVIEWER_POSTFX_SCENE_ORIENTATION: KEEP_Y
// Shadertoy per-pass iChannel bindings for Image
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler0 : register(s2);
#define iResolution (float3(PostFx_GetRenderTargetSize().xy, 1.0))
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    float2 uv = fragCoord / iResolution.xy;
    return iChannel0.Sample(glslSampler0, uv);
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "already_flipped_scene.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(!result.adaptedSource.contains("float2 BO3_PostFxSceneUV(float2 uv) { return uv; }"))
            return "KEEP_Y orientation marker did not preserve the authored resolvedScene Y orientation";
        if(result.adaptedSource.contains("return float2(uv.x, 1.0 - uv.y);"))
            return "KEEP_Y orientation marker still emitted the Shadertoy-to-D3D scene Y flip";
        if(!result.diagnostics.toText().contains("no additional Y flip was applied", Qt::CaseInsensitive))
            return "orientation diagnostics did not report the preserved upper-left scene mapping";
        return {};
    });

    run("resolvedScene orientation propagates through converted Texture2D helpers", [&]() -> QString
    {
        const QString source = R"(
// BO3_PREVIEWER_POSTFX_SCENE_ORIENTATION: FLIP_Y
// Shadertoy per-pass iChannel bindings for Image
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler : register(s1);
#define iResolution (float3(PostFx_GetRenderTargetSize().xy, 1.0))
#define GLSL_TEXTURE(tex, uv) tex.Sample(glslSampler, (uv))
float3 tex2D(Texture2D<float4> _tex, float2 _p)
{
    return GLSL_TEXTURE(_tex, _p).xyz;
}
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    float2 uv = fragCoord / iResolution.xy;
    float3 col = tex2D(iChannel0, uv);
    return float4(col, 1.0);
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "helper_forwarded_scene.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(!result.adaptedSource.contains("BO3_SCENE_TEXTURE(_tex, _p)"))
            return "Texture2D helper parameter did not inherit resolvedScene sampling semantics";
        if(!result.adaptedSource.contains("float2 BO3_PostFxSceneUV(float2 uv) { return float2(uv.x, 1.0 - uv.y); }"))
            return "FLIP_Y helper-forwarded scene did not retain the Shadertoy-to-D3D orientation bridge";
        if(!result.diagnostics.toText().contains("ADAPTER_POSTFX_SCENE_HELPER_PROPAGATED", Qt::CaseInsensitive))
            return "helper-forwarding adaptation was not reported in diagnostics";
        return {};
    });

    run("temporary PostFX prunes resources absent from optimized reflection", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler : register(s1);
SamplerState glslSampler0 : register(s2);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    return iChannel0.Sample(glslSampler0, input.texcoord);
}
)";
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.configuration = PackageConfiguration::Runtime;
        request.source = source;
        request.sourceFileName = "optimized_prune.hlsl";
        const PreviewPackageState before = session.rebuild(request);
        if(before.confidence != AutomationConfidence::Auto ||
           !before.adapter.techset.findParameter("frameBuffer"))
            return "fixture did not contain the source-declared legacy resource before reflection pruning";
        const quint64 generation = before.generation;
        const QString fingerprint = before.stateFingerprint;
        const bool changed = session.pruneOptimizedOutPostFxResources(
            QSet<QString>{"iChannel0", "glslSampler0"});
        const PreviewPackageState& after = session.state();
        if(!changed) return "optimized reflection did not prune absent package resources";
        if(after.adapter.techset.findParameter("frameBuffer") ||
           after.adapter.techset.findParameter("glslSampler"))
            return "optimized-out resource parameter survived temporary techset pruning";
        for(const TechniqueModel& technique : after.adapter.techset.techniques)
            for(const StageResourceBindingModel& binding : technique.pixelShader.resourceBindings)
                if(binding.parameterName.compare("frameBuffer", Qt::CaseInsensitive) == 0)
                    return "optimized-out Runtime binding survived temporary techset pruning";
        if(after.generation <= generation || after.stateFingerprint == fingerprint)
            return "reflection pruning did not invalidate the temporary package fingerprint";
        return {};
    });

    run("Runtime auto PostFX never binds a commented phantom frameBuffer", [&]() -> QString
    {
        const QString source = R"(
// Shadertoy per-pass iChannel bindings for Buffer A
// Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> iChannel0 : register(t2);
SamplerState glslSampler0 : register(s2);
// SamplerState glslSampler : register(s1);
struct PS_INPUT { float4 position:SV_Position; float2 texcoord:TEXCOORD0; };
float4 ps_main(PS_INPUT input):SV_Target
{
    return iChannel0.Sample(glslSampler0, input.texcoord);
}
)";
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "no_phantom_framebuffer.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(result.techset.findParameter("frameBuffer") || result.techset.findParameter("glslSampler"))
            return "auto techset recreated a commented-out legacy resource";
        for(const TechniqueModel& technique : result.techset.techniques)
        {
            for(const StageResourceBindingModel& binding : technique.pixelShader.resourceBindings)
                if(binding.parameterName.compare("frameBuffer", Qt::CaseInsensitive) == 0)
                    return "Runtime auto techset emitted a phantom frameBuffer binding";
        }
        return {};
    });

    run("temporary Preview As packages are built without disk pollution", [&]() -> QString
    {
        QTemporaryDir directory;
        if(!directory.isValid()) return "could not create no-disk preview fixture";
        const QStringList before = QDir(directory.path()).entryList(
            QDir::Files | QDir::NoDotAndDotDot);
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.configuration = PackageConfiguration::Runtime;
        request.source = autoPostFxSource;
        request.sourceFileName = "temporary_postfx.hlsl";
        const PreviewPackageState state = session.rebuild(request);
        request.target = ShaderPreviewMode::Hlsl;
        session.rebuild(request);
        request.target = ShaderPreviewMode::Material;
        request.source = materialPreviewSource;
        request.adapterSource = materialPreviewSource;
        session.rebuild(request);
        request.target = ShaderPreviewMode::Skybox;
        request.source = skyPreviewSource;
        request.adapterSource = skyPreviewSource;
        session.rebuild(request);
        const QStringList after = QDir(directory.path()).entryList(
            QDir::Files | QDir::NoDotAndDotDot);
        if(state.origin != PreviewPackageOrigin::TemporaryAdapted ||
           state.confidence != AutomationConfidence::Auto ||
           !state.packageValidationRequired ||
           state.temporaryTechsetText.trimmed().isEmpty() || !state.canPersist())
            return state.adapter.diagnostics.toText();
        return before == after ? QString()
            : "Preview As wrote an adapted HLSL or techset to disk";
    });

    run("strict temporary PostFX refuses an unsafe vertex contract", [&]() -> QString
    {
        const QString unsafeSource = R"(
struct DirectionInput { float4 position:SV_Position; float3 direction:TEXCOORD0; };
float4 ps_main(DirectionInput p):SV_Target{return float4(p.direction,1);})";
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.source = unsafeSource;
        request.sourceFileName = "unsafe_postfx.hlsl";
        const PreviewPackageState& state = session.rebuild(request);
        return state.confidence == AutomationConfidence::Unsupported &&
               !state.canPersist() &&
               hasCode(state.adapter.diagnostics, "ADAPTER_POSTFX_VS_UNSAFE")
            ? QString() : "unsafe PostFX input was given a fabricated fullscreen package";
    });

    run("temporary Material package validates with reflected constants", [&]() -> QString
    {
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::Material;
        request.source = materialPreviewSource;
        request.adapterSource = materialPreviewSource;
        request.sourceFileName = "temporary_material.hlsl";
        const PreviewPackageState& state = session.rebuild(request);
        if(state.confidence != AutomationConfidence::Auto || !state.canPersist())
            return state.adapter.diagnostics.toText();
        const ParameterModel* constant = state.adapter.techset.findParameter("materialStrength");
        if(!constant || constant->kind != ParameterKind::Float1 || !constant->hasTweak)
            return "a reflected material constant was not represented in the temporary techset";
        const ParameterModel* outputScale =
            state.adapter.techset.findParameter("bo3MaterialOutputScale");
        if(!outputScale || outputScale->properties.value("x") != "<cg31_w>" ||
           outputScale->properties.contains("w"))
            return "float1 override did not map parameter x to the selected cg storage component";
        return validateInlineAdapterPackage(state.adapter, PackageConfiguration::Runtime);
    });

    run("guided Material mapping rebuilds temporary state", [&]() -> QString
    {
        QString source = materialPreviewSource;
        source.replace("diffuse", "mysteryTexture");
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::Material;
        request.source = source;
        request.adapterSource = source;
        request.sourceFileName = "guided_material.hlsl";
        const PreviewPackageState initial = session.rebuild(request);
        if(initial.confidence != AutomationConfidence::Guided ||
           !initial.hasGuidedMappings() || initial.canPersist())
            return "ambiguous material image did not enter GUIDED state";
        const PreviewPackageState resolved = session.applyMapping(
            "mysteryTexture", "color");
        if(resolved.confidence != AutomationConfidence::Auto ||
           resolved.generation <= initial.generation ||
           resolved.stateFingerprint == initial.stateFingerprint ||
           !resolved.canPersist())
            return "explicit material role did not rebuild the live package state";
        return validateInlineAdapterPackage(resolved.adapter,
                                            PackageConfiguration::Runtime);
    });

    run("temporary directional Skybox package validates", [&]() -> QString
    {
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::Skybox;
        request.source = skyPreviewSource;
        request.adapterSource = skyPreviewSource;
        request.sourceFileName = "temporary_sky.hlsl";
        const PreviewPackageState& state = session.rebuild(request);
        if(state.confidence != AutomationConfidence::Auto || !state.canPersist() ||
           state.adapter.techset.techniques.isEmpty() ||
           state.adapter.techset.techniques.front().state != "sky")
            return state.adapter.diagnostics.toText();
        return validateInlineAdapterPackage(state.adapter,
                                            PackageConfiguration::Runtime);
    });

    run("Preview As target switching cannot retain stale package state", [&]() -> QString
    {
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.source = autoPostFxSource;
        request.sourceFileName = "switching.hlsl";
        request.target = ShaderPreviewMode::Hlsl;
        const PreviewPackageState hlslFirst = session.rebuild(request);
        request.target = ShaderPreviewMode::PostFx;
        const PreviewPackageState postfx = session.rebuild(request);
        request.target = ShaderPreviewMode::Material;
        request.adapterSource = materialPreviewSource;
        const PreviewPackageState material = session.rebuild(request);
        request.target = ShaderPreviewMode::Skybox;
        request.adapterSource = skyPreviewSource;
        const PreviewPackageState sky = session.rebuild(request);
        request.target = ShaderPreviewMode::Hlsl;
        request.adapterSource.clear();
        const PreviewPackageState hlslLast = session.rebuild(request);
        if(!(hlslFirst.generation < postfx.generation &&
             postfx.generation < material.generation &&
             material.generation < sky.generation &&
             sky.generation < hlslLast.generation))
            return "target transition did not rebuild the session generation";
        if(postfx.target != ShaderPreviewMode::PostFx ||
           material.target != ShaderPreviewMode::Material ||
           sky.target != ShaderPreviewMode::Skybox ||
           hlslLast.origin != PreviewPackageOrigin::TemporaryGeneric ||
           !hlslLast.temporaryTechsetText.isEmpty())
            return "a prior target's techset or stages leaked into the next target";
        return {};
    });

    run("shader switching clears temporary preview association", [&]() -> QString
    {
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.source = autoPostFxSource;
        request.sourceFileName = "shader_a.hlsl";
        const quint64 generation = session.rebuild(request).generation;
        session.clear();
        return session.state().origin == PreviewPackageOrigin::None &&
               session.state().source.isEmpty() &&
               session.state().temporaryTechsetText.isEmpty() &&
               session.state().generation > generation
            ? QString() : "temporary package association survived a shader load reset";
    });

    run("Save Package state preserves the validated live adaptation", [&]() -> QString
    {
        PreviewPackageSession session;
        PreviewPackageRequest request;
        request.target = ShaderPreviewMode::PostFx;
        request.source = autoPostFxSource;
        request.sourceFileName = "temporary_postfx.hlsl";
        const PreviewPackageState previewState = session.rebuild(request);
        const PackageAdapterResult persistent =
            session.persistentResult("saved_postfx.hlsl");
        if(persistent.adaptedSource != previewState.adaptedSource ||
           persistent.interfaceFingerprint != previewState.adapter.interfaceFingerprint ||
           persistent.mappings.size() != previewState.adapter.mappings.size() ||
           session.state().sourceFileName != "temporary_postfx.hlsl")
            return "persistence regenerated or mutated the live preview adaptation";
        const QString document = serializeAutoTechset(persistent);
        const TechsetParseResult parsed = parseTechset(
            document, "saved_postfx.techsetdef", {});
        const TechniqueResolutionResult resolved = resolveTechnique(parsed.model, "lit");
        if(parsed.validation.hasErrors() || !resolved.found)
            return parsed.validation.toText() + resolved.validation.toText();
        return resolved.technique.vertexShader.source == "saved_postfx.hlsl" &&
               resolved.technique.pixelShader.source == "saved_postfx.hlsl"
            ? QString() : "persistence changed more than the destination source reference";
    });

    run("PostFX auto package with scene depth samplers and generated VS", [&]() -> QString
    {
        PackageAdapterRequest request;
        request.target = PackageTarget::PostFx;
        request.source = autoPostFxSource;
        request.sourceFileName = "effect_bo3_postfx.hlsl";
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto)
            return result.diagnostics.toText();
        if(!result.generatedVertexStage || !result.adaptedSource.contains("PostFx_GenerateFullscreenQuad"))
            return "safe missing fullscreen VS was not generated";
        if(!result.techset.findParameter("frameBuffer") ||
           !result.techset.findParameter("DepthSampler") ||
           !result.techset.findParameter("frameBufferSampler") ||
           !result.techset.findParameter("DepthSamplerState"))
            return "generated techset omitted a reflected resource";
        if(result.techset.techniques.isEmpty() ||
           result.techset.techniques.front().source != "effect_bo3_postfx.hlsl")
            return "generated source path did not use the package shader basename";

        const QString document = serializeAutoTechset(result);
        TechsetParseOptions runtimeOptions;
        runtimeOptions.configuration = PackageConfiguration::Runtime;
        const auto runtimeParsed = parseTechset(document, "effect.techsetdef", runtimeOptions);
        const auto runtimeResolved = resolveTechnique(runtimeParsed.model, "lit");
        if(runtimeParsed.validation.hasErrors() || !runtimeResolved.found)
            return runtimeParsed.validation.toText() + runtimeResolved.validation.toText();
        if(runtimeResolved.technique.pixelShader.resourceBindings.size() != 2)
            return "runtime CodeTexture bindings were not generated for resolvedScene + floatZ";

        QString compilableSource = result.adaptedSource;
        compilableSource.replace(
            "#include \"postfx/postfx_common.h\"",
            "void PostFx_GenerateFullscreenQuad(float3 p, float2 uv, uint instance, out float4 position, out float2 texcoord) "
            "{ position=float4(p.xy,0,1); texcoord=uv; }");
        CompiledShaderInterface vertex;
        CompiledShaderInterface pixel;
        const QString compileFailure = compilePair(compilableSource, vertex, pixel);
        if(!compileFailure.isEmpty()) return compileFailure;
        BO3ShaderPackage package;
        package.name = "auto PostFX package";
        package.techsetSource = runtimeParsed.preprocessedSource;
        package.techset = runtimeParsed.model;
        package.configuration = PackageConfiguration::Runtime;
        package.selectedTechnique = "lit";
        package.resolvedTechnique = runtimeResolved.technique;
        package.sources[result.sourceFileName] = {
            result.sourceFileName, result.adaptedSource, {}};
        package.vertexInterface = vertex;
        package.pixelInterface = pixel;
        const ValidationResult packageValidation = validatePackage(package);
        if(packageValidation.status() != CompatibilityStatus::Pass)
            return packageValidation.toText();

        TechsetParseOptions toolsOptions;
        toolsOptions.configuration = PackageConfiguration::Toolsgfx;
        const auto toolsParsed = parseTechset(document, "effect.techsetdef", toolsOptions);
        const auto toolsResolved = resolveTechnique(toolsParsed.model, "lit");
        return toolsResolved.found && toolsResolved.technique.pixelShader.resourceBindings.isEmpty()
            ? QString() : "TOOLSGFX did not retain material-image fallbacks";
    });

    run("PostFX authored VS is preserved", [&]() -> QString
    {
        QString source = autoPostFxSource;
        source.prepend("#include \"postfx/postfx_common.h\"\nstruct VS_INPUT { float3 p:POSITION; float2 uv:TEXCOORD0; };\n"
                       "PS_INPUT vs_main(VS_INPUT v, uint i:INSTANCE_SEMANTIC) { PS_INPUT o; PostFx_GenerateFullscreenQuad(v.p,v.uv,i,o.position,o.texcoord); return o; }\n");
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "authored.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        return result.confidence == AutomationConfidence::Auto &&
               !result.generatedVertexStage &&
               result.adaptedSource.count("vs_main") == 1
            ? QString() : "authored PostFX vs_main was replaced or duplicated";
    });

    run("unknown PostFX texture and sampler require guided mapping", [&]() -> QString
    {
        QString source = autoPostFxSource;
        source.replace("frameBuffer", "mysteryTexture");
        source.replace("frameBufferSampler", "glslSampler");
        source.remove(QRegularExpression("DepthSampler[^;]*;"));
        source.remove(QRegularExpression("SamplerState DepthSamplerState[^;]*;"));
        source.replace(QRegularExpression("\\+\\s*DepthSampler[\\s\\S]*?;"), ";");
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      source, "guided.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        return result.confidence == AutomationConfidence::Guided &&
               hasCode(result.diagnostics, "ADAPTER_NEEDS_MAPPING")
            ? QString() : "ambiguous PostFX resources were guessed automatically";
    });

    run("auto techset staleness and origin metadata", [&]() -> QString
    {
        PackageAdapterRequest request{PackageTarget::PostFx, PackageConfiguration::Runtime,
                                      autoPostFxSource, "stale.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        const QString document = serializeAutoTechset(result);
        const AutoTechsetMetadata metadata = readAutoTechsetMetadata(document);
        if(metadata.origin != TechsetOrigin::AutoGenerated || !metadata.valid ||
           metadata.interfaceFingerprint != result.interfaceFingerprint)
            return "auto techset metadata could not be read";
        if(isAutoTechsetStale(document, result.interfaceFingerprint))
            return "unchanged auto techset was marked stale";
        const auto changed = analyzeShaderSource(autoPostFxSource +
            "\nTexture2D<float4> noiseTexture : register(t2);\n");
        const QString changedFingerprint = shaderInterfaceFingerprint(changed, PackageTarget::PostFx);
        if(!isAutoTechsetStale(document, changedFingerprint))
            return "changed HLSL interface did not stale the auto techset";
        return readAutoTechsetMetadata("// manual\nGlobals(){}\n").origin == TechsetOrigin::Authored
            ? QString() : "manual techset origin was not preserved";
    });

    run("Material adapter accepts generated SV_IsFrontFace pixel entry", [&]() -> QString
    {
        const QString source = R"(
// Mirrors the pixel signature emitted by makeBo3CustomMaterialShader().
struct BO3CustomMaterialPixelInput
{
    float4 position : SV_POSITION;
    float2 texCoords : TEXCOORD0;
};
float4 ps_main(const BO3CustomMaterialPixelInput pixel,
               const uint isFrontFace : SV_IsFrontFace) : SV_TARGET0
{
    return float4(pixel.texCoords, isFrontFace ? 1.0 : 0.0, 1.0);
})";
        const ShaderSourceAnalysis analysis = analyzeShaderSource(source);
        if(!analysis.hasPixelEntry)
            return "generated two-argument material ps_main was not detected";
        if(analysis.pixelInputType != "BO3CustomMaterialPixelInput")
            return "generated material ps_main did not preserve its primary pixel input type";

        PackageAdapterRequest request{PackageTarget::Material, PackageConfiguration::Runtime,
                                      source, "generated_material_wrapper.hlsl", {}};
        const PackageAdapterResult result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto || result.diagnostics.hasErrors())
            return result.diagnostics.toText().isEmpty()
                ? QString("generated material wrapper did not produce an AUTO package")
                : result.diagnostics.toText();
        return {};
    });

    run("pixel entry analysis accepts direct semantic parameters", [&]() -> QString
    {
        const QString source =
            "float4 ps_main(float4 position : SV_Position) : SV_Target { return position; }";
        const ShaderSourceAnalysis analysis = analyzeShaderSource(source);
        return analysis.hasPixelEntry && analysis.pixelInputType == "float4"
            ? QString() : "ps_main(float4 position : SV_Position) was not detected";
    });

    run("Material adapter exposes APE-visible texture parameters", [&]() -> QString
    {
        const QString source = R"(
Texture2D diffuse : register(t0); Texture2D normalMap : register(t1);
Texture2D opacity : register(t2); SamplerState colorSampler : register(s0);
struct P { float4 position:SV_Position; float2 uv:TEXCOORD0; };
float4 ps_main(P p):SV_Target { return diffuse.Sample(colorSampler,p.uv); })";
        PackageAdapterRequest request{PackageTarget::Material, PackageConfiguration::Runtime,
                                      source, "material.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto) return result.diagnostics.toText();
        for(const QString& name : {"diffuse", "normalMap", "opacity", "colorSampler"})
            if(!result.techset.findParameter(name)) return "material parameter missing: " + name;
        const ParameterModel* diffuse = result.techset.findParameter("diffuse");
        const ParameterModel* normal = result.techset.findParameter("normalMap");
        return diffuse && diffuse->hasTweak && diffuse->properties.value("image").contains("colorMap") &&
               normal && normal->properties.value("image").contains("normalMap")
            ? QString() : "material textures were not mapped to APE-visible image fields";
    });

    run("textureless deferred material has no implicit colorMap dependency", [&]() -> QString
    {
        const QString source = R"(
// BO3_PREVIEWER_MATERIAL_SURFACE: OPAQUE
struct P { float4 position:SV_Position; };
float4 ps_main(P p):SV_Target { return float4(0.2,0.4,0.8,1.0); })";
        PackageAdapterRequest request{PackageTarget::Material, PackageConfiguration::Runtime,
                                      source, "procedural_material.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto) return result.diagnostics.toText();
        if(result.techset.includes.contains("lit_base_mid"))
            return "textureless deferred material still inherits lit_base_mid";
        if(!result.techset.includes.contains("lit_base_shaders"))
            return "textureless deferred material is missing the shader-only material base";
        if(result.techset.findParameter("colorMap"))
            return "textureless deferred material synthesized a colorMap parameter";
        for(const TechniqueModel& technique : result.techset.techniques)
        {
            if(technique.names.contains("unlit"))
                return "textureless deferred material still adds an implicit stock unlit pass";
        }
        return {};
    });

    run("ambiguous material texture remains guided", [&]() -> QString
    {
        const QString source = "Texture2D unknownMap; SamplerState colorSampler; "
                               "float4 ps_main():SV_Target{return 1;}";
        PackageAdapterRequest request{PackageTarget::Material, PackageConfiguration::Runtime,
                                      source, "material.hlsl", {}};
        return adaptShaderPackage(request).confidence == AutomationConfidence::Guided
            ? QString() : "unknown material image was assigned without guidance";
    });

    run("Skybox adapter uses a dedicated sky package", [&]() -> QString
    {
        const QString source = R"(
TextureCube colorMap; SamplerState colorSampler;
struct SkyInput { float4 position:SV_Position; float3 skyDirection:TEXCOORD0; };
float4 ps_main(SkyInput p):SV_Target{return colorMap.Sample(colorSampler,p.skyDirection);})";
        PackageAdapterRequest request{PackageTarget::Skybox, PackageConfiguration::Runtime,
                                      source, "sky.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        if(result.confidence != AutomationConfidence::Auto) return result.diagnostics.toText();
        if(!result.techset.includes.contains("sky_base") ||
           result.techset.includes.contains("lit_base_shaders"))
            return "sky adapter reused a PostFX template";
        if(result.techset.techniques.isEmpty() || result.techset.techniques.front().state != "sky")
            return "sky render state was not generated";
        return {};
    });

    run("fullscreen-only shader is not faked into a skybox", [&]() -> QString
    {
        QString fullscreenWithLocalDirection = autoPostFxSource;
        fullscreenWithLocalDirection.replace(
            "return frameBuffer.Sample",
            "float3 rayDirection = float3(input.texcoord, 1.0);\n    return frameBuffer.Sample");
        PackageAdapterRequest request{PackageTarget::Skybox, PackageConfiguration::Runtime,
                                      fullscreenWithLocalDirection, "not_sky.hlsl", {}};
        const auto result = adaptShaderPackage(request);
        return result.confidence == AutomationConfidence::Unsupported &&
               hasCode(result.diagnostics, "ADAPTER_SKY_COORDINATE_MODEL")
            ? QString() : "fullscreen coordinate model was silently treated as sky direction";
    });

    run("live capture source lifecycle releases stale frames", [&]() -> QString
    {
        CaptureSourceLifecycle lifecycle;
        lifecycle.start("BO3");
        if(lifecycle.diagnostics().state != LiveCaptureState::Starting ||
           lifecycle.diagnostics().frameAvailable) return "capture did not start empty";
        lifecycle.acceptFrame(1280, 720);
        if(!lifecycle.diagnostics().frameAvailable || lifecycle.diagnostics().width != 1280)
            return "first capture frame was not accepted";
        lifecycle.acceptFrame(1920, 1080);
        if(lifecycle.diagnostics().state != LiveCaptureState::Resizing ||
           lifecycle.diagnostics().width != 1920) return "capture resize was not modeled";
        lifecycle.stalled("minimized");
        if(lifecycle.diagnostics().state != LiveCaptureState::Stalled ||
           !lifecycle.diagnostics().frameAvailable)
            return "temporary frame starvation discarded the last valid frame or was not reported";
        lifecycle.sourceLost("closed");
        if(lifecycle.diagnostics().frameAvailable || lifecycle.diagnostics().width != 0)
            return "source loss retained a stale frame";
        lifecycle.stop();
        const auto stopped = lifecycle.diagnostics();
        return stopped.state == LiveCaptureState::Stopped && !stopped.frameAvailable &&
               stopped.ldrDisplayApproximation ? QString()
                                                : "capture stop did not clear the source SRV model";
    });

    summary.output += QString("Result: %1 passed, %2 failed\n").arg(summary.passed).arg(summary.failed);
    return summary;
}

} // namespace bo3
