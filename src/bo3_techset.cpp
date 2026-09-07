#include "bo3_techset.h"

#include <QRegularExpression>

namespace bo3
{
namespace
{

struct Token
{
    enum class Kind { Identifier, String, Symbol, Newline, End };
    Kind kind = Kind::End;
    QString text;
    int line = 1;
    int column = 1;
};

class Lexer
{
public:
    explicit Lexer(const QString& source) : source_(source) {}

    QVector<Token> run()
    {
        QVector<Token> result;
        while(index_ < source_.size())
        {
            const QChar c = source_[index_];
            if(c == '\r') { advance(); continue; }
            if(c == '\n')
            {
                result.push_back({Token::Kind::Newline, "\n", line_, column_});
                advanceLine();
                continue;
            }
            if(c.isSpace()) { advance(); continue; }
            if(c == '/' && peek(1) == '/')
            {
                while(index_ < source_.size() && source_[index_] != '\n') advance();
                continue;
            }
            if(c == '/' && peek(1) == '*')
            {
                advance(); advance();
                while(index_ < source_.size())
                {
                    if(source_[index_] == '*' && peek(1) == '/') { advance(); advance(); break; }
                    if(source_[index_] == '\n') advanceLine(); else advance();
                }
                continue;
            }
            if(c == '"')
            {
                const int startLine = line_, startColumn = column_;
                advance();
                QString text;
                while(index_ < source_.size())
                {
                    const QChar current = source_[index_];
                    if(current == '"') { advance(); break; }
                    if(current == '\\' && index_ + 1 < source_.size())
                    {
                        const QChar escaped = source_[index_ + 1];
                        if(escaped == '"' || escaped == '\\') { text += escaped; advance(); advance(); continue; }
                    }
                    text += current;
                    if(current == '\n') advanceLine(); else advance();
                }
                result.push_back({Token::Kind::String, text, startLine, startColumn});
                continue;
            }
            if(c.isLetter() || c == '_' || c == '$')
            {
                const int start = index_, startLine = line_, startColumn = column_;
                while(index_ < source_.size())
                {
                    const QChar current = source_[index_];
                    if(!(current.isLetterOrNumber() || current == '_' || current == '$')) break;
                    advance();
                }
                result.push_back({Token::Kind::Identifier, source_.mid(start, index_ - start), startLine, startColumn});
                continue;
            }

            const int startLine = line_, startColumn = column_;
            QString symbol(c);
            if((c == '+' || c == '=' || c == '!' || c == '<' || c == '>') && peek(1) == '=')
            {
                symbol += peek(1);
                advance();
            }
            advance();
            result.push_back({Token::Kind::Symbol, symbol, startLine, startColumn});
        }
        result.push_back({Token::Kind::End, {}, line_, column_});
        return result;
    }

private:
    QChar peek(int offset) const
    {
        const int wanted = index_ + offset;
        return wanted >= 0 && wanted < source_.size() ? source_[wanted] : QChar();
    }
    void advance() { ++index_; ++column_; }
    void advanceLine() { ++index_; ++line_; column_ = 1; }

    const QString& source_;
    int index_ = 0;
    int line_ = 1;
    int column_ = 1;
};

SourceLocation locationFor(const QString& sourceName, const Token& token)
{
    return {sourceName, token.line, token.column};
}

QString normalizedDefineValue(QString value)
{
    value = value.trimmed();
    if(value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.mid(1, value.size() - 2);
    return value;
}

struct PreprocessResult
{
    QString source;
    QStringList includes;
    ValidationResult validation;
};

PreprocessResult preprocess(const QString& source, const QString& sourceName,
                            const TechsetParseOptions& options)
{
    struct Frame { bool parentActive; bool condition; bool known; bool elseSeen; int line; };
    QVector<Frame> stack;
    bool active = true;
    QMap<QString, QString> defines = options.defines;
    defines["TOOLSGFX"] = options.configuration == PackageConfiguration::Toolsgfx ? "1" : "0";

    PreprocessResult result;
    const QStringList lines = source.split('\n', Qt::KeepEmptyParts);
    const QRegularExpression includeRe(R"(^\s*#\s*include\s*[\"<]([^\">]+)[\">])");
    const QRegularExpression directiveRe(R"(^\s*#\s*(\w+)\b(.*)$)");
    const QRegularExpression comparisonRe(R"(^\s*([A-Za-z_]\w*)\s*(==|!=)\s*(?:\"([^\"]*)\"|([^\s]+))\s*$)");
    const QRegularExpression simpleRe(R"(^\s*([A-Za-z_]\w*)\s*$)");

    for(int i = 0; i < lines.size(); ++i)
    {
        const QString& line = lines[i];
        const auto directive = directiveRe.match(line);
        if(!directive.hasMatch())
        {
            if(active) result.source += line;
            result.source += '\n';
            continue;
        }

        const QString command = directive.captured(1).toLower();
        const QString argument = directive.captured(2).trimmed();
        if(command == "include")
        {
            if(active)
            {
                const auto match = includeRe.match(line);
                if(match.hasMatch()) result.includes << match.captured(1);
                else result.validation.add(DiagnosticLevel::Error, "TECHSET_BAD_INCLUDE",
                                           "Malformed #include directive.", {sourceName, i + 1, 1});
            }
        }
        else if(command == "if")
        {
            bool known = false, condition = false;
            const auto comparison = comparisonRe.match(argument);
            if(comparison.hasMatch())
            {
                const QString name = comparison.captured(1);
                const QString expected = !comparison.captured(3).isNull()
                    ? comparison.captured(3) : normalizedDefineValue(comparison.captured(4));
                if(defines.contains(name))
                {
                    known = true;
                    condition = normalizedDefineValue(defines.value(name)) == expected;
                    if(comparison.captured(2) == "!=") condition = !condition;
                }
            }
            else
            {
                const auto simple = simpleRe.match(argument);
                if(simple.hasMatch() && defines.contains(simple.captured(1)))
                {
                    known = true;
                    const QString value = normalizedDefineValue(defines.value(simple.captured(1))).toLower();
                    condition = !(value.isEmpty() || value == "0" || value == "false");
                }
            }
            if(!known)
            {
                result.validation.add(DiagnosticLevel::Unknown, "TECHSET_UNKNOWN_CONDITION",
                    QString("Cannot prove preprocessor condition '%1'; the false branch is selected conservatively.").arg(argument),
                    {sourceName, i + 1, 1});
            }
            stack.push_back({active, condition, known, false, i + 1});
            active = active && condition;
        }
        else if(command == "else")
        {
            if(stack.isEmpty())
                result.validation.add(DiagnosticLevel::Error, "TECHSET_UNMATCHED_ELSE", "Unmatched #else.", {sourceName, i + 1, 1});
            else
            {
                Frame& frame = stack.back();
                if(frame.elseSeen)
                    result.validation.add(DiagnosticLevel::Error, "TECHSET_DUPLICATE_ELSE", "Duplicate #else.", {sourceName, i + 1, 1});
                frame.elseSeen = true;
                active = frame.parentActive && !frame.condition;
            }
        }
        else if(command == "endif")
        {
            if(stack.isEmpty())
                result.validation.add(DiagnosticLevel::Error, "TECHSET_UNMATCHED_ENDIF", "Unmatched #endif.", {sourceName, i + 1, 1});
            else
            {
                active = stack.back().parentActive;
                stack.pop_back();
            }
        }
        else
        {
            result.validation.add(DiagnosticLevel::Unknown, "TECHSET_UNSUPPORTED_DIRECTIVE",
                QString("Unsupported preprocessor directive '#%1'.").arg(command), {sourceName, i + 1, 1});
        }
        result.source += '\n';
    }
    if(!stack.isEmpty())
        result.validation.add(DiagnosticLevel::Error, "TECHSET_UNTERMINATED_IF",
                              "Unterminated #if block.", {sourceName, stack.back().line, 1});
    result.includes.removeDuplicates();
    return result;
}

ParameterKind parameterKind(const QString& name)
{
    const QString lower = name.toLower();
    if(lower == "sampler") return ParameterKind::Sampler;
    if(lower == "texture") return ParameterKind::Texture;
    if(lower == "float1") return ParameterKind::Float1;
    if(lower == "float2") return ParameterKind::Float2;
    if(lower == "float3") return ParameterKind::Float3;
    if(lower == "float4") return ParameterKind::Float4;
    if(lower == "uint1") return ParameterKind::UInt1;
    if(lower == "uint2") return ParameterKind::UInt2;
    if(lower == "uint3") return ParameterKind::UInt3;
    if(lower == "uint4") return ParameterKind::UInt4;
    if(lower == "bool") return ParameterKind::Bool;
    if(lower == "color") return ParameterKind::Color;
    return ParameterKind::Unknown;
}

class Parser
{
public:
    Parser(QVector<Token> tokens, QString sourceName, ValidationResult* validation)
        : tokens_(std::move(tokens)), sourceName_(std::move(sourceName)), validation_(validation) {}

    void parse(TechsetModel& model)
    {
        while(!atEnd())
        {
            skipNewlines();
            if(atEnd()) break;
            if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
            const Token start = take();
            const QString keyword = start.text;
            if(keyword.compare("Globals", Qt::CaseInsensitive) == 0)
                parseGlobals(model.globals, start);
            else if(keyword.compare("Technique", Qt::CaseInsensitive) == 0)
                parseTechnique(model, start);
            else if(parameterKind(keyword) != ParameterKind::Unknown)
                parseParameter(model, start, parameterKind(keyword));
            else
            {
                validation_->add(DiagnosticLevel::Unknown, "TECHSET_UNSUPPORTED_TOP_LEVEL",
                    QString("Unsupported top-level techset construct '%1'.").arg(keyword), locationFor(sourceName_, start));
                skipLineOrBlock();
            }
        }
    }

private:
    struct Value
    {
        QVector<Token> tokens;
        QString raw;
        QString scalar;
        QString functionName;
        QStringList stringArguments;
    };

    const Token& peek(int offset = 0) const
    {
        const int wanted = qBound(0, pos_ + offset, tokens_.size() - 1);
        return tokens_[wanted];
    }
    bool atEnd() const { return peek().kind == Token::Kind::End; }
    bool is(Token::Kind kind, const QString& text = {}) const
    {
        if(peek().kind != kind) return false;
        return text.isEmpty() || peek().text.compare(text, Qt::CaseInsensitive) == 0;
    }
    bool symbol(const QString& value) const { return is(Token::Kind::Symbol) && peek().text == value; }
    Token take() { return tokens_[pos_++]; }
    bool consumeSymbol(const QString& value)
    {
        if(!symbol(value)) return false;
        ++pos_;
        return true;
    }
    void skipNewlines() { while(is(Token::Kind::Newline)) ++pos_; }

    void error(const Token& token, const QString& code, const QString& message)
    {
        validation_->add(DiagnosticLevel::Error, code, message, locationFor(sourceName_, token));
    }

    QString tokenRaw(const Token& token) const
    {
        return token.kind == Token::Kind::String ? QString("\"%1\"").arg(token.text) : token.text;
    }

    QString joinRaw(const QVector<Token>& tokens) const
    {
        QString result;
        QString previous;
        for(const Token& token : tokens)
        {
            const QString current = tokenRaw(token);
            const bool tight = current == ")" || current == "]" || current == "}" || current == "," ||
                               current == "." || current == ">" || previous == "(" || previous == "[" ||
                               previous == "{" || previous == "." || previous == "<" || current == "(";
            if(!result.isEmpty() && !tight) result += ' ';
            result += current;
            previous = current;
        }
        return result.trimmed();
    }

    QStringList stringArgs(const QVector<Token>& tokens) const
    {
        QStringList result;
        int depth = 0;
        for(const Token& token : tokens)
        {
            if(token.kind == Token::Kind::Symbol && token.text == "(") { ++depth; continue; }
            if(token.kind == Token::Kind::Symbol && token.text == ")") { --depth; continue; }
            if(depth == 1 && token.kind == Token::Kind::String) result << token.text;
        }
        return result;
    }

    Value parseValue()
    {
        Value value;
        int paren = 0, bracket = 0, angle = 0;
        while(!atEnd())
        {
            if(is(Token::Kind::Newline) && paren == 0 && bracket == 0 && angle == 0) break;
            if(symbol("}") && paren == 0 && bracket == 0 && angle == 0) break;
            const Token token = take();
            if(token.kind == Token::Kind::Symbol)
            {
                if(token.text == "(") ++paren;
                else if(token.text == ")" && paren > 0) --paren;
                else if(token.text == "[") ++bracket;
                else if(token.text == "]" && bracket > 0) --bracket;
                else if(token.text == "<") ++angle;
                else if(token.text == ">" && angle > 0) --angle;
            }
            value.tokens << token;
        }
        value.raw = joinRaw(value.tokens);
        if(value.tokens.size() == 1 && (value.tokens[0].kind == Token::Kind::String || value.tokens[0].kind == Token::Kind::Identifier))
            value.scalar = value.tokens[0].text;
        if(value.tokens.size() >= 2 && value.tokens[0].kind == Token::Kind::Identifier &&
           value.tokens[1].kind == Token::Kind::Symbol && value.tokens[1].text == "(")
        {
            value.functionName = value.tokens[0].text;
            value.stringArguments = stringArgs(value.tokens);
        }
        return value;
    }

    QStringList parseCallArguments()
    {
        QStringList arguments;
        if(!consumeSymbol("(")) return arguments;
        int depth = 1;
        QVector<Token> current;
        while(!atEnd() && depth > 0)
        {
            Token token = take();
            if(token.kind == Token::Kind::Symbol && token.text == "(") { ++depth; current << token; continue; }
            if(token.kind == Token::Kind::Symbol && token.text == ")")
            {
                --depth;
                if(depth == 0) break;
                current << token;
                continue;
            }
            if(depth == 1 && token.kind == Token::Kind::Symbol && token.text == ",")
            {
                arguments << scalarFromTokens(current);
                current.clear();
                continue;
            }
            if(token.kind != Token::Kind::Newline) current << token;
        }
        if(!current.isEmpty()) arguments << scalarFromTokens(current);
        return arguments;
    }

    QString scalarFromTokens(const QVector<Token>& tokens) const
    {
        if(tokens.size() == 1 && (tokens[0].kind == Token::Kind::String || tokens[0].kind == Token::Kind::Identifier))
            return tokens[0].text;
        return joinRaw(tokens);
    }

    QString parseOptionalBase()
    {
        skipNewlines();
        if(!consumeSymbol(":")) return {};
        skipNewlines();
        if(is(Token::Kind::String) || is(Token::Kind::Identifier)) return take().text;
        error(peek(), "TECHSET_EXPECTED_BASE", "Expected an inheritance base name after ':'.");
        return {};
    }

    bool enterBlock(const Token& owner)
    {
        skipNewlines();
        if(consumeSymbol("{")) return true;
        error(owner, "TECHSET_EXPECTED_BLOCK", QString("Expected a block after '%1'.").arg(owner.text));
        return false;
    }

    void parseGlobals(GlobalsModel& globals, const Token& start)
    {
        globals.location = locationFor(sourceName_, start);
        parseCallArguments();
        if(!enterBlock(start)) return;
        while(!atEnd() && !symbol("}"))
        {
            skipNewlines();
            if(symbol("}")) break;
            if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
            const Token field = take();
            if(!consumeSymbol("=")) { skipLineOrBlock(); continue; }
            Value value = parseValue();
            QMap<QString, QString> nested;
            const int saved = pos_;
            skipNewlines();
            if(symbol("{")) nested = parsePropertyBlock(); else pos_ = saved;
            const QString key = field.text;
            globals.properties[key] = value.scalar.isEmpty() ? value.raw : value.scalar;
            if(key.compare("category", Qt::CaseInsensitive) == 0) globals.category = value.scalar;
            else if(key.compare("availablePrefixes", Qt::CaseInsensitive) == 0) globals.availablePrefixes = value.scalar;
            else if(key.compare("renderFlags", Qt::CaseInsensitive) == 0)
            {
                globals.renderFlagsText = value.scalar.isEmpty() ? value.raw : value.scalar;
                globals.renderFlags = nested;
            }
        }
        consumeSymbol("}");
    }

    QMap<QString, QString> parsePropertyBlock(TweakModel* tweak = nullptr)
    {
        QMap<QString, QString> properties;
        if(!consumeSymbol("{")) return properties;
        while(!atEnd() && !symbol("}"))
        {
            skipNewlines();
            if(symbol("}")) break;
            if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
            Token field = take();
            if(!consumeSymbol("=")) { skipLineOrBlock(); continue; }
            Value value = parseValue();
            const QString stored = value.scalar.isEmpty() ? value.raw : value.scalar;
            properties[field.text] = stored;
            if(tweak)
            {
                if(field.text.compare("category", Qt::CaseInsensitive) == 0) tweak->category = stored;
                else if(field.text.compare("title", Qt::CaseInsensitive) == 0) tweak->title = stored;
                else if(field.text.compare("order", Qt::CaseInsensitive) == 0 ||
                        field.text.compare("sortindex", Qt::CaseInsensitive) == 0) tweak->order = stored;
                else tweak->properties[field.text] = stored;
            }
        }
        consumeSymbol("}");
        return properties;
    }

    void applyTweakValue(ParameterModel& parameter, const Value& value)
    {
        if(value.functionName.compare("Tweak", Qt::CaseInsensitive) != 0) return;
        parameter.hasTweak = true;
        if(value.stringArguments.size() > 0) parameter.tweak.category = value.stringArguments[0];
        if(value.stringArguments.size() > 1) parameter.tweak.title = value.stringArguments[1];
        if(value.stringArguments.size() > 2) parameter.tweak.order = value.stringArguments[2];
    }

    void parseParameter(TechsetModel& model, const Token& start, ParameterKind kind)
    {
        ParameterModel parameter;
        parameter.kind = kind;
        parameter.location = locationFor(sourceName_, start);
        const QStringList arguments = parseCallArguments();
        if(arguments.isEmpty())
        {
            error(start, "TECHSET_PARAMETER_NAME", QString("%1 requires a parameter name.").arg(start.text));
            skipLineOrBlock();
            return;
        }
        parameter.name = arguments[0];
        parameter.baseName = parseOptionalBase();
        skipNewlines();
        if(consumeSymbol("."))
        {
            const Token property = take();
            if(!consumeSymbol("=")) error(property, "TECHSET_EXPECTED_ASSIGNMENT", "Expected '=' after declaration property.");
            const Value value = parseValue();
            parameter.properties[property.text] = value.raw;
            if(property.text.compare("tweak", Qt::CaseInsensitive) == 0) applyTweakValue(parameter, value);
        }
        else if(symbol("{"))
        {
            consumeSymbol("{");
            while(!atEnd() && !symbol("}"))
            {
                skipNewlines();
                if(symbol("}")) break;
                if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
                const Token field = take();
                if(!consumeSymbol("=")) { skipLineOrBlock(); continue; }
                Value value = parseValue();
                if(kind == ParameterKind::Sampler &&
                   field.text.compare("filter", Qt::CaseInsensitive) == 0 &&
                   !(value.tokens.size() == 1 && value.tokens[0].kind == Token::Kind::String))
                {
                    error(field, "TECHSET_SAMPLER_FILTER_QUOTED",
                          "Sampler filter values must be quoted (for example: filter = \"linear (mip none)\").");
                }
                if(kind == ParameterKind::Float1)
                {
                    const QString component = field.text.toLower();
                    if((component == "y" || component == "z" || component == "w"))
                    {
                        error(field, "TECHSET_FLOAT1_FIELD_X",
                              QString("BO3 float1 exposes only field 'x'; use x = <cg##_%1> to read packed %1 storage.")
                                  .arg(component));
                    }
                }
                parameter.properties[field.text] = value.scalar.isEmpty() ? value.raw : value.scalar;
                if(field.text.compare("tweak", Qt::CaseInsensitive) == 0)
                {
                    applyTweakValue(parameter, value);
                    const int saved = pos_;
                    skipNewlines();
                    if(symbol("{")) parsePropertyBlock(&parameter.tweak); else pos_ = saved;
                }
            }
            consumeSymbol("}");
        }
        model.parameters << parameter;
    }

    QStringList definesFromValue(const Value& value) const
    {
        QStringList result;
        for(const Token& token : value.tokens)
            if(token.kind == Token::Kind::String) result << token.text;
        if(result.isEmpty() && !value.scalar.isEmpty()) result << value.scalar;
        result.removeDuplicates();
        return result;
    }

    void parseTechnique(TechsetModel& model, const Token& start)
    {
        TechniqueModel technique;
        technique.location = locationFor(sourceName_, start);
        technique.vertexShader.kind = StageKind::Vertex;
        technique.pixelShader.kind = StageKind::Pixel;
        technique.names = parseCallArguments();
        if(technique.names.isEmpty())
        {
            error(start, "TECHSET_TECHNIQUE_NAME", "Technique requires at least one name.");
            skipLineOrBlock();
            return;
        }
        technique.baseName = parseOptionalBase();
        if(!enterBlock(start)) return;
        while(!atEnd() && !symbol("}"))
        {
            skipNewlines();
            if(symbol("}")) break;
            if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
            const Token field = take();
            if(!consumeSymbol("=") && !consumeSymbol("+=")) { skipLineOrBlock(); continue; }
            const QString op = tokens_[pos_ - 1].text;
            if(field.text.compare("vs", Qt::CaseInsensitive) == 0 || field.text.compare("ps", Qt::CaseInsensitive) == 0)
            {
                ShaderStageModel& stage = field.text.compare("vs", Qt::CaseInsensitive) == 0
                    ? technique.vertexShader : technique.pixelShader;
                parseStageAssignment(stage, field);
                continue;
            }
            const Value value = parseValue();
            const QString stored = value.scalar.isEmpty() ? value.raw : value.scalar;
            if(field.text.compare("state", Qt::CaseInsensitive) == 0)
            {
                technique.state = stored;
                technique.stateAssigned = true;
            }
            else if(field.text.compare("source", Qt::CaseInsensitive) == 0)
            {
                technique.source = stored;
                technique.sourceAssigned = true;
            }
            else if(field.text.compare("defines", Qt::CaseInsensitive) == 0)
            {
                if(op == "+=") technique.definesAppend += definesFromValue(value);
                else { technique.defines = definesFromValue(value); technique.definesAssigned = true; }
            }
            else technique.properties[field.text] = stored;
        }
        consumeSymbol("}");
        model.techniques << technique;
    }

    void parseStageAssignment(ShaderStageModel& stage, const Token& field)
    {
        skipNewlines();
        stage.location = locationFor(sourceName_, field);
        if(is(Token::Kind::String))
        {
            stage.assignment = StageAssignmentKind::GenericName;
            stage.genericName = take().text;
            return;
        }
        if(!is(Token::Kind::Identifier))
        {
            error(peek(), "TECHSET_STAGE_VALUE", QString("Expected shader name or shader block for '%1'.").arg(field.text));
            skipLineOrBlock();
            return;
        }
        const Token constructor = take();
        const bool correct = (stage.kind == StageKind::Vertex && constructor.text.compare("VertexShader", Qt::CaseInsensitive) == 0) ||
                             (stage.kind == StageKind::Pixel && constructor.text.compare("PixelShader", Qt::CaseInsensitive) == 0);
        if(!correct)
            validation_->add(DiagnosticLevel::Unknown, "TECHSET_STAGE_CONSTRUCTOR",
                             QString("Unsupported shader-stage constructor '%1'.").arg(constructor.text),
                             locationFor(sourceName_, constructor));
        stage.assignment = StageAssignmentKind::InlineShader;
        parseCallArguments();
        stage.baseName = parseOptionalBase();
        if(!enterBlock(constructor)) return;
        while(!atEnd() && !symbol("}"))
        {
            skipNewlines();
            if(symbol("}")) break;
            if(!is(Token::Kind::Identifier)) { skipLineOrBlock(); continue; }
            const Token name = take();
            if(!consumeSymbol("=") && !consumeSymbol("+=")) { skipLineOrBlock(); continue; }
            const QString op = tokens_[pos_ - 1].text;
            const Value value = parseValue();
            const QString stored = value.scalar.isEmpty() ? value.raw : value.scalar;
            if(name.text.compare("source", Qt::CaseInsensitive) == 0)
            {
                stage.source = stored;
                stage.sourceAssigned = true;
            }
            else if(name.text.compare("entry", Qt::CaseInsensitive) == 0 || name.text.compare("entryPoint", Qt::CaseInsensitive) == 0)
                stage.entryPoint = stored;
            else if(name.text.compare("defines", Qt::CaseInsensitive) == 0)
            {
                const QStringList parsed = definesFromValue(value);
                if(op == "+=") stage.defines += parsed;
                else { stage.defines = parsed; stage.definesAssigned = true; }
            }
            else
            {
                StageResourceBindingModel binding;
                binding.parameterName = name.text;
                binding.location = locationFor(sourceName_, name);
                binding.valueName = value.stringArguments.value(0);
                if(value.functionName.compare("Texture", Qt::CaseInsensitive) == 0) binding.valueKind = BindingValueKind::Texture;
                else if(value.functionName.compare("CodeTexture", Qt::CaseInsensitive) == 0) binding.valueKind = BindingValueKind::CodeTexture;
                else if(value.functionName.compare("Sampler", Qt::CaseInsensitive) == 0) binding.valueKind = BindingValueKind::Sampler;
                else { binding.valueKind = BindingValueKind::Literal; binding.valueName = stored; }
                const int saved = pos_;
                skipNewlines();
                if(symbol("{")) binding.properties = parsePropertyBlock(); else pos_ = saved;
                stage.resourceBindings << binding;
            }
        }
        consumeSymbol("}");
    }

    void skipLineOrBlock()
    {
        int depth = 0;
        while(!atEnd())
        {
            if(is(Token::Kind::Newline) && depth == 0) { ++pos_; return; }
            if(symbol("{") ) { ++depth; ++pos_; continue; }
            if(symbol("}"))
            {
                if(depth == 0) return;
                --depth; ++pos_; continue;
            }
            ++pos_;
        }
    }

    QVector<Token> tokens_;
    QString sourceName_;
    ValidationResult* validation_ = nullptr;
    int pos_ = 0;
};

void mergeDefines(QStringList& target, const QStringList& additions)
{
    for(const QString& define : additions)
        if(!target.contains(define)) target << define;
}

ResolvedShaderStage mergeStage(const ResolvedShaderStage& base, const ShaderStageModel& child,
                               const QString& techniqueSource, const QStringList& techniqueDefines)
{
    ResolvedShaderStage result = base;
    result.kind = child.kind;
    if(child.assignment != StageAssignmentKind::Unset)
    {
        result.assignment = child.assignment;
        result.genericName = child.genericName;
        result.baseName = child.baseName;
        result.resourceBindings = child.resourceBindings;
        result.inheritanceChain.clear();
        if(!child.baseName.isEmpty()) result.inheritanceChain << child.baseName;
    }
    if(child.sourceAssigned) result.source = child.source;
    if(!child.entryPoint.isEmpty()) result.entryPoint = child.entryPoint;
    if(child.definesAssigned) result.defines = child.defines;
    else mergeDefines(result.defines, child.defines);
    if(result.source.isEmpty()) result.source = techniqueSource;

    QStringList combined = techniqueDefines;
    mergeDefines(combined, result.defines);
    result.defines = combined;
    if(result.entryPoint.isEmpty()) result.entryPoint = result.kind == StageKind::Vertex ? "vs_main" : "ps_main";
    return result;
}

bool resolveTechniqueRecursive(const TechsetModel& techset, const TechniqueModel& current,
                               QSet<const TechniqueModel*>& visiting, ResolvedTechnique& result,
                               ValidationResult& validation)
{
    if(visiting.contains(&current))
    {
        validation.add(DiagnosticLevel::Error, "TECHSET_TECHNIQUE_CYCLE",
                       QString("Technique inheritance cycle reaches '%1'.").arg(current.names.value(0)), current.location);
        return false;
    }
    visiting.insert(&current);

    ResolvedTechnique base;
    if(!current.baseName.isEmpty())
    {
        const TechniqueModel* parent = techset.findTechnique(current.baseName);
        if(parent)
        {
            if(!resolveTechniqueRecursive(techset, *parent, visiting, base, validation)) return false;
        }
        else
        {
            base.inheritanceChain << current.baseName;
            validation.add(DiagnosticLevel::Unknown, "TECHSET_EXTERNAL_TECHNIQUE_BASE",
                QString("Technique base '%1' is supplied by a stock include and cannot be resolved from this package alone.").arg(current.baseName),
                current.location);
        }
    }

    result = base;
    result.aliases = current.names;
    result.inheritanceChain << current.names.value(0);
    if(current.stateAssigned) result.renderState = interpretRenderState(current.state);
    if(current.sourceAssigned) result.source = current.source;
    if(current.definesAssigned) result.defines = current.defines;
    mergeDefines(result.defines, current.definesAppend);
    result.vertexShader = mergeStage(base.vertexShader, current.vertexShader, result.source, result.defines);
    result.pixelShader = mergeStage(base.pixelShader, current.pixelShader, result.source, result.defines);
    visiting.remove(&current);
    return true;
}

} // namespace

TechsetParseResult parseTechset(const QString& source, const QString& sourceName,
                                const TechsetParseOptions& options)
{
    TechsetParseResult result;
    result.model.sourceName = sourceName;
    const PreprocessResult preprocessed = preprocess(source, sourceName, options);
    result.preprocessedSource = preprocessed.source;
    result.model.includes = preprocessed.includes;
    result.validation.append(preprocessed.validation);
    Lexer lexer(result.preprocessedSource);
    Parser parser(lexer.run(), sourceName, &result.validation);
    parser.parse(result.model);
    result.model.diagnostics = result.validation.diagnostics;
    return result;
}

TechniqueResolutionResult resolveTechnique(const TechsetModel& techset, const QString& selectedName)
{
    TechniqueResolutionResult result;
    const TechniqueModel* selected = techset.findTechnique(selectedName);
    if(!selected)
    {
        result.validation.add(DiagnosticLevel::Error, "TECHSET_TECHNIQUE_NOT_FOUND",
                              QString("Technique '%1' was not found.").arg(selectedName), {techset.sourceName, 0, 0});
        return result;
    }
    result.found = true;
    result.technique.selectedName = selectedName;
    QSet<const TechniqueModel*> visiting;
    if(!resolveTechniqueRecursive(techset, *selected, visiting, result.technique, result.validation))
        result.found = false;
    result.technique.selectedName = selectedName;
    return result;
}

bool resolveParameter(const TechsetModel& techset, const QString& name, ParameterModel& resolved,
                      ValidationResult* validation)
{
    const ParameterModel* current = techset.findParameter(name);
    if(!current) return false;
    QVector<const ParameterModel*> chain;
    QSet<const ParameterModel*> seen;
    while(current)
    {
        if(seen.contains(current))
        {
            if(validation) validation->add(DiagnosticLevel::Error, "TECHSET_PARAMETER_CYCLE",
                QString("Parameter inheritance cycle reaches '%1'.").arg(current->name), current->location);
            return false;
        }
        seen.insert(current);
        chain.prepend(current);
        if(current->baseName.isEmpty()) break;
        current = techset.findParameter(current->baseName);
        if(!current)
        {
            if(validation) validation->add(DiagnosticLevel::Unknown, "TECHSET_EXTERNAL_PARAMETER_BASE",
                QString("Parameter base '%1' is supplied by a stock include and cannot be resolved locally.").arg(chain.front()->baseName),
                chain.front()->location);
            break;
        }
    }
    resolved = {};
    for(const ParameterModel* item : chain)
    {
        if(item->kind != ParameterKind::Unknown) resolved.kind = item->kind;
        resolved.name = item->name;
        resolved.baseName = item->baseName;
        for(auto it = item->properties.cbegin(); it != item->properties.cend(); ++it) resolved.properties[it.key()] = it.value();
        if(item->hasTweak) { resolved.tweak = item->tweak; resolved.hasTweak = true; }
        resolved.location = item->location;
    }
    resolved.name = name;
    return true;
}

} // namespace bo3
