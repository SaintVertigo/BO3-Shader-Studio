#include "shadertoy_project.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace ShadertoyProject
{
namespace
{
struct Macro
{
    QString name;
    QStringList parameters;
    QString body;
    bool functionLike = false;
    bool uncertain = false;
};

struct Function
{
    QString name;
    QStringList parameterNames;
    QStringList parameterTypes;
    QString body;
    bool fromCommon = false;
    int definitionStart = -1;
    int definitionEnd = -1;
};

struct Call
{
    QString name;
    QStringList arguments;
    int start = -1;
    int end = -1;
};

QString stripComments(const QString& source)
{
    QString result = source;
    bool lineComment = false;
    bool blockComment = false;
    bool quoted = false;
    QChar quote;
    for(int i = 0; i < result.size(); ++i)
    {
        const QChar c = result[i];
        const QChar next = i + 1 < result.size() ? result[i + 1] : QChar();
        if(lineComment)
        {
            if(c == '\n') lineComment = false;
            else result[i] = ' ';
            continue;
        }
        if(blockComment)
        {
            if(c == '*' && next == '/')
            {
                result[i] = result[i + 1] = ' ';
                ++i;
                blockComment = false;
            }
            else if(c != '\n') result[i] = ' ';
            continue;
        }
        if(quoted)
        {
            if(c == '\\') { ++i; continue; }
            if(c == quote) quoted = false;
            continue;
        }
        if(c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if(c == '/' && next == '/')
        {
            result[i] = result[i + 1] = ' ';
            ++i;
            lineComment = true;
        }
        else if(c == '/' && next == '*')
        {
            result[i] = result[i + 1] = ' ';
            ++i;
            blockComment = true;
        }
    }
    return result;
}

int matchingDelimiter(const QString& source, int open)
{
    if(open < 0 || open >= source.size()) return -1;
    const QChar opening = source[open];
    const QChar closing = opening == '(' ? ')' : opening == '{' ? '}' : opening == '[' ? ']' : QChar();
    if(closing.isNull()) return -1;
    int depth = 0;
    bool quoted = false;
    QChar quote;
    for(int i = open; i < source.size(); ++i)
    {
        const QChar c = source[i];
        if(quoted)
        {
            if(c == '\\') { ++i; continue; }
            if(c == quote) quoted = false;
            continue;
        }
        if(c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if(c == opening) ++depth;
        else if(c == closing && --depth == 0) return i;
    }
    return -1;
}

QStringList splitArguments(const QString& text)
{
    QStringList result;
    int start = 0;
    int round = 0, square = 0, curly = 0;
    for(int i = 0; i < text.size(); ++i)
    {
        const QChar c = text[i];
        if(c == '(') ++round;
        else if(c == ')') --round;
        else if(c == '[') ++square;
        else if(c == ']') --square;
        else if(c == '{') ++curly;
        else if(c == '}') --curly;
        else if(c == ',' && round == 0 && square == 0 && curly == 0)
        {
            result << text.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    const QString last = text.mid(start).trimmed();
    if(!last.isEmpty() || !text.trimmed().isEmpty()) result << last;
    return result;
}

QString peelParentheses(QString expression)
{
    expression = expression.trimmed();
    while(expression.startsWith('('))
    {
        const int close = matchingDelimiter(expression, 0);
        if(close != expression.size() - 1) break;
        expression = expression.mid(1, expression.size() - 2).trimmed();
    }
    return expression;
}

int exactChannel(const QString& expression)
{
    static const QRegularExpression channelRe("^iChannel([0-3])$");
    const auto match = channelRe.match(peelParentheses(expression));
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QVector<Call> callsIn(const QString& text)
{
    QVector<Call> calls;
    static const QRegularExpression callRe("\\b([A-Za-z_]\\w*)\\s*\\(");
    auto iterator = callRe.globalMatch(text);
    while(iterator.hasNext())
    {
        const auto match = iterator.next();
        const int open = text.indexOf('(', match.capturedStart(0));
        const int close = matchingDelimiter(text, open);
        if(close < 0) continue;
        Call call;
        call.name = match.captured(1);
        call.arguments = splitArguments(text.mid(open + 1, close - open - 1));
        call.start = match.capturedStart(0);
        call.end = close + 1;
        calls.push_back(call);
    }
    return calls;
}

QHash<QString, Macro> parseMacros(const QString& source)
{
    QHash<QString, Macro> macros;
    QStringList lines = source.split('\n');
    for(int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        QString logical = lines[lineIndex];
        while(logical.trimmed().endsWith('\\') && lineIndex + 1 < lines.size())
        {
            logical.chop(1);
            logical += " " + lines[++lineIndex].trimmed();
        }
        static const QRegularExpression defineRe(
            "^\\s*#\\s*define\\s+([A-Za-z_]\\w*)(\\s*\\(([^)]*)\\))?\\s*(.*)$");
        const auto match = defineRe.match(logical);
        if(!match.hasMatch()) continue;
        Macro macro;
        macro.name = match.captured(1);
        macro.functionLike = !match.captured(2).isEmpty();
        if(macro.functionLike)
        {
            for(const QString& parameter : match.captured(3).split(',', Qt::SkipEmptyParts))
                macro.parameters << parameter.trimmed();
        }
        macro.body = match.captured(4).trimmed();
        macro.uncertain = macro.parameters.contains("...") || macro.body.contains("##") ||
                          QRegularExpression("(^|[^#])#\\s*[A-Za-z_]").match(macro.body).hasMatch();
        macros.insert(macro.name, macro);
    }
    return macros;
}

QString maskPreprocessor(QString source)
{
    const QStringList lines = source.split('\n', Qt::KeepEmptyParts);
    QStringList masked;
    bool continuation = false;
    for(const QString& line : lines)
    {
        const bool directive = continuation || line.trimmed().startsWith('#');
        continuation = directive && line.trimmed().endsWith('\\');
        masked << (directive ? QString(line.size(), ' ') : line);
    }
    return masked.join('\n');
}

QVector<Function> parseFunctions(const QString& original, bool fromCommon, QString* topLevel)
{
    QString source = maskPreprocessor(stripComments(original));
    QVector<Function> functions;
    static const QRegularExpression functionRe(
        "(?:^|[;}]|\\n)\\s*(?:(?:const|highp|mediump|lowp|precise|inline)\\s+)*"
        "[A-Za-z_]\\w*(?:\\s*\\[[^]]*\\])?\\s+([A-Za-z_]\\w*)\\s*\\(([^;{}]*)\\)\\s*\\{",
        QRegularExpression::MultilineOption);
    int from = 0;
    while(from < source.size())
    {
        const auto match = functionRe.match(source, from);
        if(!match.hasMatch()) break;
        const int open = source.indexOf('{', match.capturedStart(0));
        const int close = matchingDelimiter(source, open);
        if(close < 0) { from = match.capturedEnd(0); continue; }

        Function function;
        function.name = match.captured(1);
        function.body = source.mid(open + 1, close - open - 1);
        function.fromCommon = fromCommon;
        function.definitionStart = match.capturedStart(0);
        if(function.definitionStart < source.size() &&
           (source[function.definitionStart] == ';' || source[function.definitionStart] == '}'))
            ++function.definitionStart;
        function.definitionEnd = close + 1;

        for(QString parameter : splitArguments(match.captured(2)))
        {
            parameter.remove(QRegularExpression("\\[[^]]*\\]"));
            QStringList words = parameter.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            words.removeAll("const"); words.removeAll("in"); words.removeAll("out");
            words.removeAll("inout"); words.removeAll("highp"); words.removeAll("mediump");
            words.removeAll("lowp"); words.removeAll("precise");
            if(words.size() >= 2)
            {
                function.parameterTypes << words[words.size() - 2];
                function.parameterNames << words.last();
            }
            else
            {
                function.parameterTypes << QString();
                function.parameterNames << QString();
            }
        }
        functions.push_back(function);
        from = close + 1;
    }

    if(topLevel)
    {
        *topLevel = source;
        for(auto it = functions.crbegin(); it != functions.crend(); ++it)
        {
            const int length = it->definitionEnd - it->definitionStart;
            topLevel->replace(it->definitionStart, length, QString(length, ' '));
        }
        topLevel->remove(QRegularExpression(
            "\\b(?:uniform\\s+)?sampler(?:1D|2D|3D|Cube|2DArray)?\\s+iChannel[0-3]\\s*;"));
    }
    return functions;
}

QString expandMacros(QString text, const QHash<QString, Macro>& macros,
                     QSet<int>& uncertainChannels)
{
    for(int iteration = 0; iteration < 16; ++iteration)
    {
        bool changed = false;
        for(auto macroIt = macros.constBegin(); macroIt != macros.constEnd(); ++macroIt)
        {
            const Macro& macro = macroIt.value();
            const QRegularExpression tokenRe(QString("\\b%1\\b").arg(QRegularExpression::escape(macro.name)));
            if(!macro.functionLike)
            {
                if(!tokenRe.match(text).hasMatch()) continue;
                if(macro.uncertain)
                {
                    for(int channel = 0; channel < ChannelCount; ++channel)
                        if(macro.body.contains(QString("iChannel%1").arg(channel))) uncertainChannels.insert(channel);
                    continue;
                }
                text.replace(tokenRe, "(" + macro.body + ")");
                changed = true;
                continue;
            }

            QVector<QPair<int, QString>> replacements;
            auto tokenIt = tokenRe.globalMatch(text);
            while(tokenIt.hasNext())
            {
                const auto token = tokenIt.next();
                int open = token.capturedEnd(0);
                while(open < text.size() && text[open].isSpace()) ++open;
                if(open >= text.size() || text[open] != '(') continue;
                const int close = matchingDelimiter(text, open);
                if(close < 0) continue;
                const QStringList arguments = splitArguments(text.mid(open + 1, close - open - 1));
                if(macro.uncertain || arguments.size() != macro.parameters.size())
                {
                    for(const QString& argument : arguments)
                    {
                        const int channel = exactChannel(argument);
                        if(channel >= 0) uncertainChannels.insert(channel);
                    }
                    for(int channel = 0; channel < ChannelCount; ++channel)
                        if(macro.body.contains(QString("iChannel%1").arg(channel))) uncertainChannels.insert(channel);
                    continue;
                }
                QString expansion = macro.body;
                for(int parameter = 0; parameter < macro.parameters.size(); ++parameter)
                {
                    const QRegularExpression parameterRe(
                        QString("\\b%1\\b").arg(QRegularExpression::escape(macro.parameters[parameter])));
                    expansion.replace(parameterRe, "(" + arguments[parameter] + ")");
                }
                replacements.push_back(qMakePair(token.capturedStart(0),
                    QString::number(close + 1 - token.capturedStart(0)) + "\n" + "(" + expansion + ")"));
            }
            for(auto replacement = replacements.crbegin(); replacement != replacements.crend(); ++replacement)
            {
                const int separator = replacement->second.indexOf('\n');
                const int length = replacement->second.left(separator).toInt();
                text.replace(replacement->first, length, replacement->second.mid(separator + 1));
                changed = true;
            }
        }
        if(!changed) break;
    }
    return text;
}

bool isSamplerType(const QString& type)
{
    return type.contains("sampler", Qt::CaseInsensitive) ||
           type.contains("texture", Qt::CaseInsensitive);
}

QSet<int> directSampledParameters(const Function& function, const QString& expandedBody)
{
    QSet<int> result;
    static const QSet<QString> samplingFunctions = {
        "texture", "texture2D", "textureCube", "textureLod", "textureGrad",
        "textureProj", "texelFetch", "textureSize", "textureQueryLevels"
    };
    for(const Call& call : callsIn(expandedBody))
    {
        if(!samplingFunctions.contains(call.name) || call.arguments.isEmpty()) continue;
        const QString sampler = peelParentheses(call.arguments.first());
        for(int parameter = 0; parameter < function.parameterNames.size(); ++parameter)
        {
            if(isSamplerType(function.parameterTypes.value(parameter)) &&
               sampler == function.parameterNames[parameter])
                result.insert(parameter);
        }
    }
    return result;
}

void addReason(ChannelAnalysis& analysis, const QString& reason)
{
    if(!analysis.reasons.contains(reason)) analysis.reasons << reason;
}

QString noiseHelperSource(const QString& helperName)
{
    return QString(
        "// BO3 standalone replacement for a generic Shadertoy noise texture.\n"
        "float bo3_standalone_hash12(vec2 p)\n"
        "{\n"
        "    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);\n"
        "    p3 += dot(p3, p3.yzx + 33.33);\n"
        "    return fract((p3.x + p3.y) * p3.z);\n"
        "}\n"
        "vec4 %1(vec2 uv)\n"
        "{\n"
        "    float n = bo3_standalone_hash12(floor(uv * 256.0));\n"
        "    return vec4(n, n, n, 1.0);\n"
        "}\n\n").arg(helperName);
}

struct RewriteResult
{
    QString source;
    int replacements = 0;
};

RewriteResult rewriteSampling(const QString& original, int channel,
                              bool procedural, const QString& helperName)
{
    RewriteResult result{original, 0};
    static const QSet<QString> samplingFunctions = {
        "texture", "texture2D", "textureCube", "textureLod", "textureGrad",
        "textureProj", "texelFetch", "textureSize", "textureQueryLevels"
    };
    const QString scan = stripComments(original);
    QVector<Call> matches;
    for(const Call& call : callsIn(scan))
    {
        if(samplingFunctions.contains(call.name) && !call.arguments.isEmpty() &&
           exactChannel(call.arguments.first()) == channel)
            matches.push_back(call);
    }
    for(auto it = matches.crbegin(); it != matches.crend(); ++it)
    {
        QString replacement;
        if(it->name == "textureSize") replacement = procedural ? "ivec2(256)" : "ivec2(1)";
        else if(it->name == "textureQueryLevels") replacement = "1";
        else if(procedural)
        {
            QString coordinates = it->arguments.value(1, "vec2(0.0)");
            if(it->name == "texelFetch") coordinates = "(vec2(" + coordinates + ") / 256.0)";
            replacement = helperName + "(" + coordinates + ")";
        }
        else replacement = "vec4(0.0)";
        result.source.replace(it->start, it->end - it->start, replacement);
        ++result.replacements;
    }
    return result;
}
}

QString usageName(ChannelUsage usage)
{
    switch(usage)
    {
        case ChannelUsage::AssignedUnused: return "Assigned / Unused";
        case ChannelUsage::Referenced: return "Referenced";
        case ChannelUsage::Used: return "Used";
        case ChannelUsage::PossiblyUsed: return "Possibly Used";
    }
    return "Assigned / Unused";
}

QString projectModeName(ProjectMode mode)
{
    switch(mode)
    {
        case ProjectMode::Preserve: return "Preserve Shadertoy Inputs";
        case ProjectMode::Standalone: return "Make BO3 Standalone";
        case ProjectMode::Custom: return "Custom Per Channel";
    }
    return "Make BO3 Standalone";
}

QString channelModeName(ChannelMode mode)
{
    switch(mode)
    {
        case ChannelMode::Auto: return "Auto";
        case ChannelMode::Preserve: return "Preserve";
        case ChannelMode::Procedural: return "Procedural";
        case ChannelMode::Neutral: return "Neutral";
        case ChannelMode::Remove: return "Remove";
    }
    return "Auto";
}

ProjectMode projectModeFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if(normalized == "preserve") return ProjectMode::Preserve;
    if(normalized == "custom") return ProjectMode::Custom;
    return ProjectMode::Standalone;
}

ChannelMode channelModeFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if(normalized == "preserve") return ChannelMode::Preserve;
    if(normalized == "procedural") return ChannelMode::Procedural;
    if(normalized == "neutral") return ChannelMode::Neutral;
    if(normalized == "remove") return ChannelMode::Remove;
    return ChannelMode::Auto;
}

PassAnalysis analyze(const QString& commonSource, const QString& passSource)
{
    PassAnalysis result;
    const QString strippedCommon = stripComments(commonSource);
    const QString strippedPass = stripComments(passSource);
    const QHash<QString, Macro> macros = parseMacros(strippedCommon + "\n" + strippedPass);

    QString commonTopLevel;
    QString passTopLevel;
    QVector<Function> functions = parseFunctions(strippedCommon, true, &commonTopLevel);
    const QVector<Function> passFunctions = parseFunctions(strippedPass, false, &passTopLevel);
    functions += passFunctions;

    QHash<QString, QVector<int>> functionsByName;
    for(int index = 0; index < functions.size(); ++index)
        functionsByName[functions[index].name].push_back(index);

    QSet<int> reachable;
    for(int index = 0; index < functions.size(); ++index)
    {
        if(functions[index].fromCommon) continue;
        if(functions[index].name == "mainImage" || functions[index].name == "main")
            reachable.insert(index);
    }
    if(reachable.isEmpty())
    {
        for(int index = 0; index < functions.size(); ++index)
            if(!functions[index].fromCommon) reachable.insert(index);
    }

    for(int iteration = 0; iteration < functions.size() + 2; ++iteration)
    {
        bool changed = false;
        const QList<int> current = reachable.values();
        for(int functionIndex : current)
        {
            QSet<int> graphUncertainty;
            const QString graphBody = expandMacros(functions[functionIndex].body, macros, graphUncertainty);
            for(const Call& call : callsIn(graphBody))
            {
                for(int target : functionsByName.value(call.name))
                    if(!reachable.contains(target)) { reachable.insert(target); changed = true; }
            }
        }
        if(!changed) break;
    }

    QString reachableText = passTopLevel;
    QVector<QString> expandedBodies(functions.size());
    QSet<int> uncertainChannels;
    for(int index : reachable)
    {
        expandedBodies[index] = expandMacros(functions[index].body, macros, uncertainChannels);
        reachableText += "\n" + expandedBodies[index];
    }
    reachableText = expandMacros(reachableText, macros, uncertainChannels);

    static const QSet<QString> samplingFunctions = {
        "texture", "texture2D", "textureCube", "textureLod", "textureGrad",
        "textureProj", "texelFetch", "textureSize", "textureQueryLevels"
    };
    for(const Call& call : callsIn(reachableText))
    {
        if(!samplingFunctions.contains(call.name) || call.arguments.isEmpty()) continue;
        const int channel = exactChannel(call.arguments.first());
        if(channel < 0) continue;
        ChannelAnalysis& analysis = result.channels[channel];
        analysis.usage = ChannelUsage::Used;
        analysis.directRead = true;
        addReason(analysis, QString("sampled by %1").arg(call.name));
    }

    QVector<QSet<int>> sampledParameters(functions.size());
    for(int index : reachable)
        sampledParameters[index] = directSampledParameters(functions[index], expandedBodies[index]);
    for(int iteration = 0; iteration < functions.size() + 2; ++iteration)
    {
        bool changed = false;
        for(int caller : reachable)
        {
            for(const Call& call : callsIn(expandedBodies[caller]))
            {
                for(int callee : functionsByName.value(call.name))
                {
                    if(!reachable.contains(callee)) continue;
                    for(int sampledParameter : sampledParameters[callee])
                    {
                        const QString argument = peelParentheses(call.arguments.value(sampledParameter));
                        const int callerParameter = functions[caller].parameterNames.indexOf(argument);
                        if(callerParameter >= 0 &&
                           isSamplerType(functions[caller].parameterTypes.value(callerParameter)) &&
                           !sampledParameters[caller].contains(callerParameter))
                        {
                            sampledParameters[caller].insert(callerParameter);
                            changed = true;
                        }
                    }
                }
            }
        }
        if(!changed) break;
    }

    for(const Call& call : callsIn(reachableText))
    {
        for(int callee : functionsByName.value(call.name))
        {
            if(!reachable.contains(callee)) continue;
            for(int sampledParameter : sampledParameters[callee])
            {
                const int channel = exactChannel(call.arguments.value(sampledParameter));
                if(channel < 0) continue;
                ChannelAnalysis& analysis = result.channels[channel];
                analysis.usage = ChannelUsage::Used;
                analysis.indirectRead = true;
                addReason(analysis, QString("passed to sampler helper %1").arg(call.name));
            }
        }
    }

    for(int channel = 0; channel < ChannelCount; ++channel)
    {
        ChannelAnalysis& analysis = result.channels[channel];
        const QRegularExpression channelRe(QString("\\biChannel%1\\b").arg(channel));
        if(analysis.usage != ChannelUsage::Used && uncertainChannels.contains(channel))
        {
            analysis.usage = ChannelUsage::PossiblyUsed;
            addReason(analysis, "use is hidden by an unsafe/ambiguous macro");
        }
        else if(analysis.usage != ChannelUsage::Used && channelRe.match(reachableText).hasMatch())
        {
            analysis.usage = ChannelUsage::Referenced;
            addReason(analysis, "referenced by reachable code but not sampled");
        }
    }
    return result;
}

bool isGenericNoiseAsset(const ChannelBinding& binding)
{
    const QString evidence = (binding.source + " " + binding.resourceId + " " +
                              binding.originalType).toLower();
    static const QRegularExpression noiseRe(
        "(^|[^a-z])(blue[-_ ]?noise|white[-_ ]?noise|value[-_ ]?noise|noise|random|rand|dither|hash)([^a-z]|$)");
    return binding.kind == "texture2d" && noiseRe.match(evidence).hasMatch();
}

HandlingDecision decide(const ChannelBinding& binding, ProjectMode projectMode,
                        int passIndex)
{
    HandlingDecision decision;
    if(binding.kind == "none")
    {
        decision.effectiveMode = "Unassigned";
        decision.safeToRemove = true;
        return decision;
    }

    ChannelMode requested = projectMode == ProjectMode::Custom
        ? binding.requestedMode
        : projectMode == ProjectMode::Preserve ? ChannelMode::Preserve : ChannelMode::Auto;
    const bool uncertain = binding.analysis.usage == ChannelUsage::PossiblyUsed;
    const bool used = binding.analysis.usage == ChannelUsage::Used || uncertain;

    if(requested == ChannelMode::Preserve)
    {
        decision.effectiveMode = "Preserved";
        decision.preserveInput = true;
    }
    else if(requested == ChannelMode::Remove)
    {
        if(!used && binding.analysis.usage != ChannelUsage::Referenced)
        {
            decision.effectiveMode = "Removed (unused)";
            decision.safeToRemove = true;
        }
        else
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.warning = "Remove was rejected because reachable shader code still refers to this channel.";
        }
    }
    else if(requested == ChannelMode::Neutral || requested == ChannelMode::Procedural)
    {
        if(uncertain || binding.analysis.indirectRead)
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.warning = "The channel is used through a macro or sampler helper that cannot be rewritten safely.";
        }
        else if(requested == ChannelMode::Procedural && !isGenericNoiseAsset(binding))
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.warning = "Procedural replacement is limited to assets identified as generic noise/random data.";
        }
        else
        {
            decision.effectiveMode = requested == ChannelMode::Procedural ? "Procedural noise" : "Neutralized";
            decision.rewriteSampling = used;
            decision.proceduralNoise = requested == ChannelMode::Procedural;
            decision.safeToRemove = !used;
        }
    }
    else if(!used && binding.analysis.usage != ChannelUsage::Referenced)
    {
        decision.effectiveMode = "Ignored (unused)";
        decision.safeToRemove = true;
    }
    else if(uncertain)
    {
        decision.effectiveMode = "Preserved";
        decision.preserveInput = true;
        decision.warning = "Conservative preserve: macro analysis could not prove how this channel is used.";
    }
    else if(binding.kind == "keyboard" || binding.kind == "audio")
    {
        if(binding.analysis.indirectRead)
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.warning = "Interactive input is sampled through a helper and cannot be neutralized safely.";
        }
        else
        {
            decision.effectiveMode = "Neutralized";
            decision.rewriteSampling = true;
        }
    }
    else if(isGenericNoiseAsset(binding))
    {
        if(binding.analysis.indirectRead)
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.warning = "Noise input is sampled through a helper and cannot be proceduralized safely.";
        }
        else
        {
            decision.effectiveMode = "Procedural noise";
            decision.rewriteSampling = true;
            decision.proceduralNoise = true;
        }
    }
    else
    {
        decision.effectiveMode = "Preserved";
        decision.preserveInput = true;
    }

    if(binding.bufferPassIndex == passIndex && binding.bufferPassIndex >= 2)
    {
        const QString feedbackWarning = "Feedback buffer preserved: previous-frame routing is required and cannot be flattened safely.";
        decision.warning = decision.warning.isEmpty() ? feedbackWarning : decision.warning + " " + feedbackWarning;
        decision.effectiveMode = "Preserved feedback";
        decision.preserveInput = true;
        decision.rewriteSampling = false;
    }
    return decision;
}

TransformResult transform(const QString& commonSource, const QString& passSource,
                          const QVector<ChannelBinding>& bindings,
                          ProjectMode projectMode, int passIndex)
{
    TransformResult result;
    result.commonSource = commonSource;
    result.passSource = passSource;
    QString allSource = commonSource + "\n" + passSource;
    QString helperName = "bo3_standalone_noise_sample";
    for(int suffix = 1; allSource.contains(QRegularExpression(
            QString("\\b%1\\b").arg(QRegularExpression::escape(helperName)))); ++suffix)
        helperName = QString("bo3_standalone_noise_sample_%1").arg(suffix);
    bool needsNoiseHelper = false;

    for(int channel = 0; channel < ChannelCount; ++channel)
    {
        ChannelBinding binding = bindings.value(channel);
        result.decisions[channel] = decide(binding, projectMode, passIndex);
        HandlingDecision& decision = result.decisions[channel];
        if(!decision.warning.isEmpty())
            result.notes << QString("iChannel%1: %2").arg(channel).arg(decision.warning);
        if(!decision.rewriteSampling) continue;

        const QString beforeCommon = result.commonSource;
        const QString beforePass = result.passSource;
        const RewriteResult rewrittenCommon = rewriteSampling(beforeCommon, channel, decision.proceduralNoise, helperName);
        const RewriteResult rewrittenPass = rewriteSampling(beforePass, channel, decision.proceduralNoise, helperName);
        if(rewrittenCommon.replacements + rewrittenPass.replacements == 0)
        {
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.rewriteSampling = false;
            decision.warning = "No sampling expression could be rewritten without expanding a macro globally.";
            result.notes << QString("iChannel%1: %2").arg(channel).arg(decision.warning);
            continue;
        }
        result.commonSource = rewrittenCommon.source;
        result.passSource = rewrittenPass.source;

        const PassAnalysis remaining = analyze(result.commonSource, result.passSource);
        if(remaining.channels[channel].usage == ChannelUsage::Used ||
           remaining.channels[channel].usage == ChannelUsage::PossiblyUsed)
        {
            result.commonSource = beforeCommon;
            result.passSource = beforePass;
            decision.effectiveMode = "Preserved";
            decision.preserveInput = true;
            decision.rewriteSampling = false;
            decision.warning = "Only part of the channel's reachable sampling graph was safely rewritable.";
            result.notes << QString("iChannel%1: %2").arg(channel).arg(decision.warning);
            continue;
        }
        needsNoiseHelper = needsNoiseHelper || decision.proceduralNoise;
        result.notes << QString("iChannel%1: %2 replacement applied at the sampling abstraction.")
                            .arg(channel).arg(decision.effectiveMode);
    }

    if(needsNoiseHelper)
    {
        const QString helper = noiseHelperSource(helperName);
        if(!result.commonSource.trimmed().isEmpty()) result.commonSource.prepend(helper);
        else result.passSource.prepend(helper);
    }
    return result;
}

bool shouldFetchAsset(const ChannelBinding& binding, ProjectMode projectMode,
                      int passIndex, bool explicitlyRequested)
{
    if(binding.kind != "texture2d" && binding.kind != "cubemap") return false;
    if(binding.source.trimmed().isEmpty()) return false;
    if(explicitlyRequested || (projectMode == ProjectMode::Custom &&
                               binding.requestedMode == ChannelMode::Preserve)) return true;
    if(binding.analysis.usage != ChannelUsage::Used &&
       binding.analysis.usage != ChannelUsage::PossiblyUsed) return false;
    return decide(binding, projectMode, passIndex).preserveInput;
}

QString passAnalysisSummary(const QVector<ChannelBinding>& bindings,
                            ProjectMode projectMode, int passIndex)
{
    QStringList lines;
    for(int channel = 0; channel < ChannelCount; ++channel)
    {
        const ChannelBinding binding = bindings.value(channel);
        if(binding.kind == "none")
        {
            lines << QString("iChannel%1: Unassigned").arg(channel);
            continue;
        }
        const HandlingDecision decision = decide(binding, projectMode, passIndex);
        QString line = QString("iChannel%1: %2 -> %3")
                           .arg(channel).arg(usageName(binding.analysis.usage), decision.effectiveMode);
        if(!decision.warning.isEmpty()) line += " (warning)";
        lines << line;
    }
    return lines.join("  |  ");
}
}
