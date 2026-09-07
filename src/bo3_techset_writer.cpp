#include "bo3_techset_writer.h"

#include <QRegularExpression>

namespace bo3
{
namespace
{

QString indent(int level) { return QString(level * 4, ' '); }

QString quoted(QString value)
{
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    return '"' + value + '"';
}

QString propertyValue(const QString& value)
{
    const QString trimmed = value.trimmed();
    if(trimmed.isEmpty()) return "\"\"";
    if(trimmed.startsWith('"') || trimmed.startsWith('<') ||
       // BO3 constructor expressions are emitted without whitespace before the
       // opening parenthesis (Image(...), CodeTexture(...), Sampler(...), etc.).
       // Display strings such as "linear (mip none)" must remain quoted.
       trimmed.contains(QRegularExpression(R"(^[A-Za-z_]\w*\()")) ||
       trimmed.contains(QRegularExpression(R"(^[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?$)")) ||
       trimmed.compare("true", Qt::CaseInsensitive) == 0 ||
       trimmed.compare("false", Qt::CaseInsensitive) == 0)
        return trimmed;
    return quoted(trimmed);
}

void writeProperties(QString& out, const QMap<QString, QString>& properties, int level)
{
    for(auto it = properties.cbegin(); it != properties.cend(); ++it)
        out += indent(level) + it.key() + " = " + propertyValue(it.value()) + '\n';
}

void writeTweak(QString& out, const TweakModel& tweak, int level)
{
    out += indent(level) + "tweak = Tweak()\n";
    out += indent(level) + "{\n";
    if(!tweak.category.isEmpty()) out += indent(level + 1) + "category = " + quoted(tweak.category) + '\n';
    if(!tweak.title.isEmpty()) out += indent(level + 1) + "title = " + quoted(tweak.title) + '\n';
    if(!tweak.order.isEmpty()) out += indent(level + 1) + "sortindex = " + quoted(tweak.order) + '\n';
    writeProperties(out, tweak.properties, level + 1);
    out += indent(level) + "}\n";
}

QString defineList(const QStringList& defines)
{
    QStringList values;
    for(const QString& define : defines) values << quoted(define);
    return values.join(", ");
}

void writeBinding(QString& out, const StageResourceBindingModel& binding, int level)
{
    if(!binding.preprocessorCondition.isEmpty())
        out += indent(level) + "#if " + binding.preprocessorCondition + '\n';
    QString value;
    switch(binding.valueKind)
    {
        case BindingValueKind::Texture: value = "Texture()"; break;
        case BindingValueKind::CodeTexture: value = "CodeTexture(" + quoted(binding.valueName) + ")"; break;
        case BindingValueKind::Sampler: value = "Sampler(" + quoted(binding.valueName) + ")"; break;
        case BindingValueKind::Literal: value = binding.valueName; break;
        case BindingValueKind::Unknown: value = binding.valueName; break;
    }
    out += indent(level) + binding.parameterName + " = " + value + '\n';
    if(!binding.properties.isEmpty())
    {
        out += indent(level) + "{\n";
        writeProperties(out, binding.properties, level + 1);
        out += indent(level) + "}\n";
    }
    if(!binding.preprocessorCondition.isEmpty())
        out += indent(level) + "#endif\n";
}

void writeStage(QString& out, const QString& field, const ShaderStageModel& stage, int level)
{
    if(stage.assignment == StageAssignmentKind::Unset) return;
    if(stage.assignment == StageAssignmentKind::GenericName)
    {
        out += indent(level) + field + " = " + quoted(stage.genericName) + '\n';
        return;
    }
    const QString constructor = stage.kind == StageKind::Vertex ? "VertexShader" : "PixelShader";
    out += indent(level) + field + " = " + constructor + "()";
    if(!stage.baseName.isEmpty()) out += " : " + quoted(stage.baseName);
    out += "\n" + indent(level) + "{\n";
    if(stage.sourceAssigned) out += indent(level + 1) + "source = " + quoted(stage.source) + '\n';
    if(!stage.entryPoint.isEmpty()) out += indent(level + 1) + "entryPoint = " + quoted(stage.entryPoint) + '\n';
    if(stage.definesAssigned || !stage.defines.isEmpty())
        out += indent(level + 1) + "defines = " + defineList(stage.defines) + '\n';
    for(const StageResourceBindingModel& binding : stage.resourceBindings) writeBinding(out, binding, level + 1);
    out += indent(level) + "}\n";
}

} // namespace

QString serializeTechset(const TechsetModel& techset)
{
    QString out;
    for(const QString& include : techset.includes) out += "#include " + quoted(include) + '\n';
    if(!techset.includes.isEmpty()) out += '\n';

    out += "Globals()\n{\n";
    if(!techset.globals.category.isEmpty()) out += indent(1) + "category = " + quoted(techset.globals.category) + '\n';
    if(!techset.globals.renderFlags.isEmpty())
    {
        out += indent(1) + "renderFlags = RenderFlags()\n" + indent(1) + "{\n";
        writeProperties(out, techset.globals.renderFlags, 2);
        out += indent(1) + "}\n";
    }
    else if(!techset.globals.renderFlagsText.isEmpty())
        out += indent(1) + "renderFlags = " + quoted(techset.globals.renderFlagsText) + '\n';
    if(!techset.globals.availablePrefixes.isEmpty())
        out += indent(1) + "availablePrefixes = " + quoted(techset.globals.availablePrefixes) + '\n';
    for(auto it = techset.globals.properties.cbegin(); it != techset.globals.properties.cend(); ++it)
    {
        if(it.key().compare("category", Qt::CaseInsensitive) == 0 ||
           it.key().compare("renderFlags", Qt::CaseInsensitive) == 0 ||
           it.key().compare("availablePrefixes", Qt::CaseInsensitive) == 0) continue;
        out += indent(1) + it.key() + " = " + propertyValue(it.value()) + '\n';
    }
    out += "}\n\n";

    for(const ParameterModel& parameter : techset.parameters)
    {
        out += toString(parameter.kind) + "(" + quoted(parameter.name) + ")";
        if(!parameter.baseName.isEmpty()) out += " : " + quoted(parameter.baseName);
        if(parameter.properties.isEmpty() && !parameter.hasTweak)
        {
            out += "\n{\n}\n\n";
            continue;
        }
        out += "\n{\n";
        for(auto it = parameter.properties.cbegin(); it != parameter.properties.cend(); ++it)
        {
            if(it.key().compare("tweak", Qt::CaseInsensitive) == 0) continue;
            out += indent(1) + it.key() + " = " + propertyValue(it.value()) + '\n';
        }
        if(parameter.hasTweak) writeTweak(out, parameter.tweak, 1);
        out += "}\n\n";
    }

    for(const TechniqueModel& technique : techset.techniques)
    {
        if(!technique.preprocessorCondition.isEmpty())
            out += "#if " + technique.preprocessorCondition + '\n';
        QStringList names;
        for(const QString& name : technique.names) names << quoted(name);
        out += "Technique(" + names.join(", ") + ")";
        if(!technique.baseName.isEmpty()) out += " : " + quoted(technique.baseName);
        out += "\n{\n";
        if(technique.stateAssigned) out += indent(1) + "state = " + quoted(technique.state) + '\n';
        if(technique.sourceAssigned) out += indent(1) + "source = " + quoted(technique.source) + '\n';
        if(technique.definesAssigned) out += indent(1) + "defines = " + defineList(technique.defines) + '\n';
        if(!technique.definesAppend.isEmpty()) out += indent(1) + "defines += " + defineList(technique.definesAppend) + '\n';
        writeStage(out, "vs", technique.vertexShader, 1);
        writeStage(out, "ps", technique.pixelShader, 1);
        writeProperties(out, technique.properties, 1);
        out += "}\n\n";
        if(!technique.preprocessorCondition.isEmpty()) out += "#endif\n\n";
    }
    return out;
}

} // namespace bo3
