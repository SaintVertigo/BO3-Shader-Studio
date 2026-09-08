#include "glsl_converter_core.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace bo3::glsl
{
namespace
{
// Keep the converter independent from the preview renderer. Shadertoy exposes
// four standard input channels (iChannel0..iChannel3), so the conversion core
// owns its local channel-count constant instead of depending on renderer state.
constexpr int kGlslChannelCount = 4;

class Core final
{
public:
    QString convertGlslFunctionCalls(QString source) const
    {
        auto replaceCalls = [&](const QString& functionName, const std::function<QString(const QStringList&)>& build)
        {
            int searchFrom = 0;
            while(searchFrom < source.size())
            {
                int pos = source.indexOf(functionName, searchFrom, Qt::CaseSensitive);
                if(pos < 0) break;
                const bool leftOk = pos == 0 || !(source[pos-1].isLetterOrNumber() || source[pos-1] == '_');
                int afterName = pos + functionName.size();
                const bool rightOk = afterName >= source.size() || !(source[afterName].isLetterOrNumber() || source[afterName] == '_');
                if(!leftOk || !rightOk) { searchFrom = afterName; continue; }
                while(afterName < source.size() && source[afterName].isSpace()) ++afterName;
                if(afterName >= source.size() || source[afterName] != '(') { searchFrom = afterName; continue; }

                int scanLimit = source.size();
                const int previousNewline = pos > 0 ? source.lastIndexOf('\n', pos - 1) : -1;
                const int physicalLineStart = previousNewline + 1;
                const QString linePrefix = source.mid(physicalLineStart, pos - physicalLineStart).trimmed();
                const bool startsInDirective = linePrefix.startsWith('#');
                if(startsInDirective)
                {
                    const int physicalLineEnd = source.indexOf('\n', afterName);
                    if(physicalLineEnd >= 0)
                        scanLimit = physicalLineEnd;
                }

                int depth = 0;
                int close = -1;
                for(int i = afterName; i < scanLimit; ++i)
                {
                    const QChar c = source[i];
                    if(c == '(') ++depth;
                    else if(c == ')')
                    {
                        --depth;
                        if(depth == 0) { close = i; break; }
                    }
                }
                if(close < 0)
                {
                    // Never let an unfinished preprocessor fragment consume text
                    // from later source lines while looking for its closing ')'.
                    if(startsInDirective)
                    {
                        searchFrom = scanLimit < source.size() ? scanLimit + 1 : source.size();
                        continue;
                    }
                    break;
                }

                const QString inside = source.mid(afterName + 1, close - afterName - 1);
                QStringList args;
                int argDepth = 0;
                int argStart = 0;
                for(int i = 0; i < inside.size(); ++i)
                {
                    const QChar c = inside[i];
                    if(c == '(' || c == '[' || c == '{') ++argDepth;
                    else if(c == ')' || c == ']' || c == '}') --argDepth;
                    else if(c == ',' && argDepth == 0)
                    {
                        args << inside.mid(argStart, i - argStart).trimmed();
                        argStart = i + 1;
                    }
                }
                args << inside.mid(argStart).trimmed();
                const QString replacement = build(args);
                if(replacement.isEmpty()) { searchFrom = close + 1; continue; }
                source.replace(pos, close - pos + 1, replacement);
                searchFrom = pos + replacement.size();
            }
        };

        replaceCalls("texture2D", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_TEXTURE(%1, %2)").arg(a[0], a[1]);
            if(a.size() == 3) return QString("GLSL_TEXTURE_BIAS(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });
        replaceCalls("textureCube", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_TEXTURE(%1, %2)").arg(a[0], a[1]);
            if(a.size() == 3) return QString("GLSL_TEXTURE_BIAS(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });
        replaceCalls("textureLod", [](const QStringList& a) -> QString {
            if(a.size() == 3) return QString("GLSL_TEXTURE_LEVEL(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });
        replaceCalls("textureGrad", [](const QStringList& a) -> QString {
            if(a.size() == 4) return QString("GLSL_TEXTURE_GRAD(%1, %2, %3, %4)").arg(a[0], a[1], a[2], a[3]);
            return {};
        });
        replaceCalls("texture", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_TEXTURE(%1, %2)").arg(a[0], a[1]);
            if(a.size() == 3) return QString("GLSL_TEXTURE_BIAS(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });
        replaceCalls("texelFetch", [](const QStringList& a) -> QString {
            if(a.size() >= 3) return QString("GLSL_TEXEL_FETCH(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });
        replaceCalls("textureSize", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_TEXTURE_SIZE(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("inverse", [](const QStringList& a) -> QString {
            if(a.size() == 1) return QString("GLSL_INVERSE(%1)").arg(a[0]);
            return {};
        });

        // GLSL relational builtins return component-wise boolean vectors.
        // Route them through wrapper macros so the later scalar-vector equality
        // pass does not accidentally reduce equal()/notEqual() to all()/any().
        replaceCalls("lessThan", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_LESS_THAN(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("lessThanEqual", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_LESS_THAN_EQUAL(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("greaterThan", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_GREATER_THAN(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("greaterThanEqual", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_GREATER_THAN_EQUAL(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("equal", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_EQUAL(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("notEqual", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("GLSL_NOT_EQUAL(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        replaceCalls("not", [](const QStringList& a) -> QString {
            if(a.size() == 1) return QString("GLSL_NOT(%1)").arg(a[0]);
            return {};
        });

        replaceCalls("atan", [](const QStringList& a) -> QString {
            if(a.size() == 2) return QString("atan2(%1, %2)").arg(a[0], a[1]);
            return {};
        });
        return source;
    }

    QString replaceScopedIdentifierUsesPreservingMembers(
        QString text, const QString& identifier, const QString& replacement) const
    {
        // Local-shadow renaming must never rewrite a token that is being used as
        // a struct member or vector swizzle. Example: if a local scalar `z` is
        // renamed because a same-named function exists, `p.z` still refers to the
        // z component of p and must remain exactly `p.z`. Accept whitespace around
        // the member operator as well (`p . z`).
        const QRegularExpression useRe(
            QString("\\b%1\\b").arg(QRegularExpression::escape(identifier)));

        struct Use
        {
            int start = -1;
            int length = 0;
        };
        QVector<Use> uses;

        auto it = useRe.globalMatch(text);
        while(it.hasNext())
        {
            const auto m = it.next();
            int prev = static_cast<int>(m.capturedStart()) - 1;
            while(prev >= 0 && text[prev].isSpace())
                --prev;
            if(prev >= 0 && text[prev] == '.')
                continue;

            uses.push_back({
                static_cast<int>(m.capturedStart()),
                static_cast<int>(m.capturedLength())
            });
        }

        for(int i = static_cast<int>(uses.size()) - 1; i >= 0; --i)
            text.replace(uses[i].start, uses[i].length, replacement);
        return text;
    }


    QString expandGlslShadertoyBuiltinAliasMacros(QString source, QStringList& notes) const
    {
        // GLSL's preprocessor may alias a short identifier to a Shadertoy builtin,
        // then use that short name as a local/parameter. Example:
        //     #define t iTime
        //     float fnoise(vec3 p, in float t) { ... }
        // In GLSL the preprocessor first turns the parameter into `iTime`, after
        // which normal lexical shadowing applies. BO3's generated wrapper instead
        // exposes iTime as a preprocessor macro, so leaving the alias for FXC would
        // produce an invalid declaration such as `float (GetTime())`.
        //
        // Expand only simple object-like aliases whose complete replacement is one
        // Shadertoy builtin. Remove the alias definition while preserving its line
        // width/newline, then let renameGlslShadowedBuiltins() protect any resulting
        // local/parameter shadow. Skip aliases that are later undefined because a
        // whole-source expansion would not preserve that lifetime correctly.
        const QStringList builtinNames = {
            "iResolution", "iTime", "iTimeDelta", "iFrameRate",
            "iFrame", "iMouse", "iDate"
        };
        const QString builtinAlternation = builtinNames.join('|');
        const QRegularExpression aliasRe(
            QString("^\\s*#\\s*define\\s+([A-Za-z_]\\w*)[ \\t]+(%1)[ \\t]*$")
                .arg(builtinAlternation),
            QRegularExpression::MultilineOption);

        struct AliasDef
        {
            int start = -1;
            int length = 0;
            QString alias;
            QString target;
        };

        QVector<AliasDef> defs;
        auto it = aliasRe.globalMatch(source);
        while(it.hasNext())
        {
            const auto m = it.next();
            const QString alias = m.captured(1);
            const QString target = m.captured(2);
            if(alias == target)
                continue;

            const QRegularExpression undefRe(
                QString("^\\s*#\\s*undef\\s+%1\\b")
                    .arg(QRegularExpression::escape(alias)),
                QRegularExpression::MultilineOption);
            if(undefRe.match(source).hasMatch())
                continue;

            defs.push_back({
                static_cast<int>(m.capturedStart()),
                static_cast<int>(m.capturedLength()),
                alias,
                target
            });
        }

        if(defs.isEmpty())
            return source;

        // If a name has multiple definitions, do not guess which conditional
        // preprocessor branch is active. Only deterministic one-definition aliases
        // are safe to expand in this compatibility pass.
        QHash<QString, int> counts;
        for(const AliasDef& def : defs)
            counts[def.alias] += 1;

        QVector<AliasDef> safeDefs;
        for(const AliasDef& def : defs)
        {
            if(counts.value(def.alias) == 1)
                safeDefs.push_back(def);
        }
        if(safeDefs.isEmpty())
            return source;

        // Blank the definitions from the end so source offsets stay valid. Keeping
        // the same number of characters preserves useful converter line mapping.
        for(int i = safeDefs.size() - 1; i >= 0; --i)
            source.replace(safeDefs[i].start, safeDefs[i].length,
                           QString(safeDefs[i].length, ' '));

        QStringList expanded;
        for(const AliasDef& def : safeDefs)
        {
            source.replace(
                QRegularExpression(QString("\\b%1\\b")
                    .arg(QRegularExpression::escape(def.alias))),
                def.target);
            expanded << QString("%1 -> %2").arg(def.alias, def.target);
        }

        expanded.removeDuplicates();
        if(!expanded.isEmpty())
            notes << QString("Expanded Shadertoy builtin object-alias macro(s) before FXC shadow handling: %1.")
                         .arg(expanded.join(", "));
        return source;
    }

    QString renameGlslShadowedBuiltins(QString source, QStringList& notes) const
    {
        // The generated Shadertoy wrapper exposes iResolution/iTime/etc. as
        // preprocessor macros. GLSL may legally shadow those names in a local
        // scope, for example:
        //     vec2 iResolution = iResolution.xy;
        // A macro would expand both identifiers and corrupt the declaration.
        // Rename only the local declaration and the uses after its initializer,
        // leaving the RHS bound to the real Shadertoy built-in.
        const QStringList builtinNames = {
            "iResolution", "iTime", "iTimeDelta", "iFrameRate",
            "iFrame", "iMouse", "iDate"
        };
        const QString builtinAlternation = builtinNames.join('|');
        const QRegularExpression declRe(
            QString("\\b(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234])\\s+(%1)\\b")
                .arg(builtinAlternation));

        struct ShadowDecl
        {
            int nameStart = -1;
            int nameLength = 0;
            QString builtin;
        };

        QVector<ShadowDecl> declarations;
        auto it = declRe.globalMatch(source);
        while(it.hasNext())
        {
            const auto m = it.next();
            ShadowDecl d;
            d.nameStart = m.capturedStart(1);
            d.nameLength = m.capturedLength(1);
            d.builtin = m.captured(1);
            declarations.push_back(d);
        }

        bool changed = false;
        int renameIndex = 0;
        // Work from the end of the source toward the beginning. This prevents an
        // outer shadow from renaming a nested shadow that has already received a
        // unique local name.
        for(int di = declarations.size() - 1; di >= 0; --di)
        {
            const ShadowDecl& d = declarations[di];
            if(d.nameStart < 0 || d.nameStart >= source.size()) continue;

            // Find the nearest enclosing lexical block for this declaration.
            QVector<int> braceStack;
            for(int i = 0; i < d.nameStart && i < source.size(); ++i)
            {
                if(source[i] == '{') braceStack.push_back(i);
                else if(source[i] == '}' && !braceStack.isEmpty()) braceStack.pop_back();
            }
            if(braceStack.isEmpty()) continue; // Ignore globals / malformed input.
            const int blockOpen = braceStack.back();

            // Find the end of the declaration without being confused by nested
            // constructor/function-call parentheses.
            int parenDepth = 0;
            int bracketDepth = 0;
            int declarationEnd = -1;
            for(int i = d.nameStart + d.nameLength; i < source.size(); ++i)
            {
                const QChar c = source[i];
                if(c == '(') ++parenDepth;
                else if(c == ')') --parenDepth;
                else if(c == '[') ++bracketDepth;
                else if(c == ']') --bracketDepth;
                else if(c == ';' && parenDepth == 0 && bracketDepth == 0)
                {
                    declarationEnd = i;
                    break;
                }
                else if(c == '{' && parenDepth == 0 && bracketDepth == 0)
                {
                    // This was a function parameter or another non-local-declaration
                    // match rather than a normal statement.
                    declarationEnd = -1;
                    break;
                }
            }
            if(declarationEnd < 0) continue;

            // Find the closing brace paired with the declaration's enclosing block.
            int depth = 0;
            int blockClose = -1;
            for(int i = blockOpen; i < source.size(); ++i)
            {
                if(source[i] == '{') ++depth;
                else if(source[i] == '}')
                {
                    --depth;
                    if(depth == 0)
                    {
                        blockClose = i;
                        break;
                    }
                }
            }
            if(blockClose < 0 || declarationEnd >= blockClose) continue;

            const QString localName = QString("_glsl_local_%1_%2").arg(d.builtin).arg(++renameIndex);
            source.replace(d.nameStart, d.nameLength, localName);
            const int delta = localName.size() - d.nameLength;
            declarationEnd += delta;
            blockClose += delta;

            // GLSL's shadowing declaration initializer can still reference the
            // outer built-in. Rename only uses that occur after the semicolon.
            const int scopeStart = declarationEnd + 1;
            const int scopeLength = blockClose - scopeStart;
            if(scopeLength > 0)
            {
                QString scoped = source.mid(scopeStart, scopeLength);
                scoped = replaceScopedIdentifierUsesPreservingMembers(scoped, d.builtin, localName);
                source.replace(scopeStart, scopeLength, scoped);
            }

            changed = true;
        }

        // Function parameters need the same protection. They live outside the
        // body's opening brace syntactically, so the local-declaration pass above
        // deliberately skips them. After object-like aliases such as
        // `#define t iTime` are expanded, a legal GLSL signature can become:
        //     float fnoise(vec3 p, in float iTime) { ... }
        // Rename that parameter and all of its body uses before the generated
        // `#define iTime (GetTime())` wrapper reaches FXC.
        QString scan = source;
        int lineStart = 0;
        bool continuation = false;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();
            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            const bool directive = continuation || (first < lineEnd && scan[first] == '#');
            if(directive)
            {
                for(int i = lineStart; i < lineEnd; ++i) scan[i] = ' ';
            }

            int tail = lineEnd - 1;
            while(tail >= lineStart && (source[tail] == ' ' || source[tail] == '\t')) --tail;
            continuation = directive && tail >= lineStart && source[tail] == '\\';
            lineStart = lineEnd < scan.size() ? lineEnd + 1 : scan.size();
        }

        struct FunctionShadow
        {
            int start = -1;
            int bodyOpen = -1;
            int bodyClose = -1;
            struct Parameter
            {
                int nameStart = -1;
                int nameLength = 0;
                QString builtin;
            };
            QVector<Parameter> parameters;
        };

        const QRegularExpression functionRe(
            "\\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?)|[A-Za-z_]\\w*)"
            "\\s+[A-Za-z_]\\w*\\s*\\(([^;{}]*)\\)\\s*\\{");
        const QRegularExpression parameterRe(
            QString("\\b(?:(?:const|in|out|inout|lowp|mediump|highp)\\s+)*"
                    "(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?))"
                    "\\s+(%1)\\b").arg(builtinAlternation));

        QVector<FunctionShadow> functionShadows;
        auto functionIt = functionRe.globalMatch(scan);
        while(functionIt.hasNext())
        {
            const auto function = functionIt.next();
            QVector<FunctionShadow::Parameter> params;
            const QString parameterText = source.mid(function.capturedStart(1), function.capturedLength(1));
            auto parameterIt = parameterRe.globalMatch(parameterText);
            while(parameterIt.hasNext())
            {
                const auto parameter = parameterIt.next();
                params.push_back({
                    static_cast<int>(function.capturedStart(1) + parameter.capturedStart(1)),
                    static_cast<int>(parameter.capturedLength(1)),
                    parameter.captured(1)
                });
            }
            if(params.isEmpty())
                continue;

            const int bodyOpen = function.capturedEnd(0) - 1;
            int depth = 0;
            int bodyClose = -1;
            for(int i = bodyOpen; i < scan.size(); ++i)
            {
                if(scan[i] == '{') ++depth;
                else if(scan[i] == '}')
                {
                    --depth;
                    if(depth == 0)
                    {
                        bodyClose = i;
                        break;
                    }
                }
            }
            if(bodyClose < 0)
                continue;

            FunctionShadow f;
            f.start = function.capturedStart(0);
            f.bodyOpen = bodyOpen;
            f.bodyClose = bodyClose;
            f.parameters = params;
            functionShadows.push_back(f);
        }

        // Reverse order keeps all recorded offsets valid while replacement strings
        // grow. Local shadows were already renamed, so bare builtin tokens left in
        // each body unambiguously refer to the matching parameter.
        for(int fi = functionShadows.size() - 1; fi >= 0; --fi)
        {
            const FunctionShadow& f = functionShadows[fi];
            if(f.start < 0 || f.bodyOpen < f.start || f.bodyClose <= f.bodyOpen)
                continue;

            QString header = source.mid(f.start, f.bodyOpen - f.start + 1);
            QString body = source.mid(f.bodyOpen + 1, f.bodyClose - f.bodyOpen - 1);

            struct HeaderEdit { int start; int length; QString replacement; };
            QVector<HeaderEdit> headerEdits;
            for(const auto& parameter : f.parameters)
            {
                const QString localName = QString("_glsl_local_%1_%2")
                                              .arg(parameter.builtin)
                                              .arg(++renameIndex);
                body = replaceScopedIdentifierUsesPreservingMembers(body, parameter.builtin, localName);
                headerEdits.push_back({
                    parameter.nameStart - f.start,
                    parameter.nameLength,
                    localName
                });
            }

            std::sort(headerEdits.begin(), headerEdits.end(), [](const HeaderEdit& a, const HeaderEdit& b)
            {
                return a.start > b.start;
            });
            for(const HeaderEdit& edit : headerEdits)
                header.replace(edit.start, edit.length, edit.replacement);

            source.replace(f.start, f.bodyClose - f.start + 1, header + body + "}");
            changed = true;
        }

        if(changed)
            notes << "Renamed locally/parameter-shadowed Shadertoy built-ins to avoid BO3/FXC preprocessor macro collisions.";
        return source;
    }

    QString renameGlslHlslIntrinsicCollisions(QString source, QStringList& notes) const
    {
        // Some GLSL identifiers are perfectly legal until conversion changes a
        // DIFFERENT GLSL builtin into the same HLSL spelling. Example:
        //
        //     vec2 frac = fract(uv);
        //
        // Naively translating fract -> frac produces:
        //
        //     float2 frac = frac(uv);
        //
        // FXC then resolves the local variable instead of the HLSL intrinsic and
        // reports X3005 ("identifier represents a variable, not a function").
        // Rename user symbols that collide with HLSL spellings BEFORE token and
        // function-call translation. Because GLSL uses different spellings for
        // the converted builtins, an exact-word replacement here does not touch
        // fract(), mix(), inversesqrt(), dFdx(), etc.
        const QStringList collisionNames = {
            "frac", "lerp", "rsqrt", "ddx", "ddy",
            "asuint", "asint", "asfloat", "atan2", "mul"
        };

        const QString collisionAlternation = collisionNames.join('|');
        const QRegularExpression declarationRe(
            QString("\\b(?:const\\s+)?(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?))\\s+(%1)\\b")
                .arg(collisionAlternation));

        QSet<QString> declaredCollisions;
        auto it = declarationRe.globalMatch(source);
        while(it.hasNext())
            declaredCollisions.insert(it.next().captured(1));

        // Function-like/object-like preprocessor macros can collide with HLSL
        // intrinsics just as variables/functions can. This matters especially for
        // shader libraries that define helpers such as `#define mul(a,b) ...`; if
        // left intact, later matrix lowering cannot safely call HLSL's intrinsic
        // mul(). GLSL itself has no builtin named mul, so renaming the user's macro
        // and all of its source uses is semantics-preserving.
        const QRegularExpression macroDeclarationRe(
            QString("^\\s*#\\s*define\\s+(%1)\\b")
                .arg(collisionAlternation),
            QRegularExpression::MultilineOption);
        auto mit = macroDeclarationRe.globalMatch(source);
        while(mit.hasNext())
            declaredCollisions.insert(mit.next().captured(1));

        if(declaredCollisions.isEmpty())
            return source;

        QStringList renamed;
        for(const QString& name : collisionNames)
        {
            if(!declaredCollisions.contains(name))
                continue;

            QString replacement = QString("_glsl_hlsl_%1").arg(name);
            int suffix = 1;
            while(source.contains(QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(replacement)))))
                replacement = QString("_glsl_hlsl_%1_%2").arg(name).arg(++suffix);

            source.replace(
                QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(name))),
                replacement);
            renamed << name;
        }

        if(!renamed.isEmpty())
            notes << QString("Renamed GLSL symbol(s) that would collide with translated HLSL intrinsics: %1.")
                         .arg(renamed.join(", "));

        return source;
    }

    
    
    QString normalizeGlslEmptyFunctionMacros(QString source, QStringList& notes) const
    {
        // Legacy FXC rejects some zero-parameter function-like macro definitions
        // even though GLSL preprocessors accept them:
        //     #define returnPos() ...
        // Give the macro one unused parameter and pass a harmless zero at call
        // sites. The macro body is unchanged.
        const QRegularExpression defRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s*\\(\\s*\\)",
            QRegularExpression::MultilineOption);

        QStringList names;
        auto it = defRe.globalMatch(source);
        while(it.hasNext())
            names << it.next().captured(1);
        names.removeDuplicates();

        bool changed = false;
        for(const QString& name : names)
        {
            source.replace(
                QRegularExpression(
                    QString("(^\\s*#define\\s+%1\\s*)\\(\\s*\\)")
                        .arg(QRegularExpression::escape(name)),
                    QRegularExpression::MultilineOption),
                "\\1(_glsl_unused)");

            source.replace(
                QRegularExpression(
                    QString("\\b%1\\s*\\(\\s*\\)")
                        .arg(QRegularExpression::escape(name))),
                QString("%1(0)").arg(name));
            changed = true;
        }

        if(changed)
            notes << QString("Normalized zero-parameter GLSL macros for legacy FXC: %1.")
                         .arg(names.join(", "));
        return source;
    }


    QString normalizeGlslEmptyMacroArguments(QString source, QStringList& notes) const
    {
        // GLSL preprocessors allow an empty token sequence to be supplied to a
        // one-parameter function-like macro:
        //
        //     #define D(S) (1.0 + (S 2.0))
        //     D( )
        //
        // Legacy FXC expands an "empty marker" macro before argument counting,
        // so D(GLSL_EMPTY_MACRO_ARG) still produces X1516. Instead, expand only
        // the empty invocation ourselves using the macro replacement list with
        // its single formal parameter replaced by an empty token sequence. Calls
        // that contain a real argument (D(-), D(+), etc.) remain preprocessor
        // macros and preserve their original behavior.
        struct MacroDef
        {
            QString name;
            QString parameter;
            QString body;
        };

        QVector<MacroDef> defs;
        const QRegularExpression headerRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s*\\(\\s*([A-Za-z_]\\w*)\\s*\\)\\s*(.*)$");

        int lineStart = 0;
        while(lineStart < source.size())
        {
            int lineEnd = source.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = source.size();

            const QString firstLine = source.mid(lineStart, lineEnd - lineStart);
            const auto header = headerRe.match(firstLine);
            if(!header.hasMatch())
            {
                lineStart = (lineEnd < source.size()) ? lineEnd + 1 : source.size();
                continue;
            }

            QString body = header.captured(3);
            int continuationEnd = lineEnd;

            auto stripContinuation = [](QString& text) -> bool
            {
                int i = text.size() - 1;
                while(i >= 0 && (text[i] == ' ' || text[i] == '\t')) --i;
                if(i >= 0 && text[i] == '\\')
                {
                    text.remove(i, 1);
                    return true;
                }
                return false;
            };

            bool continued = stripContinuation(body);
            while(continued && continuationEnd < source.size())
            {
                const int nextStart = continuationEnd + 1;
                int nextEnd = source.indexOf('\n', nextStart);
                if(nextEnd < 0) nextEnd = source.size();

                QString nextLine = source.mid(nextStart, nextEnd - nextStart);
                continued = stripContinuation(nextLine);
                body += "\n" + nextLine;
                continuationEnd = nextEnd;
            }

            defs.push_back({header.captured(1), header.captured(2), body});
            lineStart = (continuationEnd < source.size()) ? continuationEnd + 1 : source.size();
        }

        QStringList changedNames;
        for(const MacroDef& def : defs)
        {
            const QRegularExpression emptyCallRe(
                QString("\\b%1\\s*\\(\\s*\\)")
                    .arg(QRegularExpression::escape(def.name)));

            if(!emptyCallRe.match(source).hasMatch())
                continue;

            QString expanded = def.body;
            expanded.replace(
                QRegularExpression(
                    QString("\\b%1\\b")
                        .arg(QRegularExpression::escape(def.parameter))),
                QString());

            // Do not attempt to emulate stringification/token-pasting here. GLSL
            // shader macros using an empty argument in the corpus are ordinary
            // replacement-list macros; preserving #/## semantics would require a
            // full preprocessor and is better left visible than guessed.
            if(expanded.contains("##") ||
               QRegularExpression(QString("(^|[^#])#\\s*%1\\b")
                   .arg(QRegularExpression::escape(def.parameter))).match(def.body).hasMatch())
            {
                continue;
            }

            source.replace(emptyCallRe, expanded);
            changedNames << def.name;
        }

        changedNames.removeDuplicates();
        if(!changedNames.isEmpty())
            notes << QString("Expanded empty GLSL macro argument invocation(s) for legacy FXC: %1.")
                         .arg(changedNames.join(", "));

        return source;
    }


    QString normalizeTwiglCompatibilityAliases(QString source, QStringList& notes) const
    {
        // TwiGL geek/geeker/geekest shaders commonly omit uniform declarations
        // and use single-character builtins: r=resolution, t=time, f=frame,
        // m=mouse, plus o for the fragment output.  Only activate this fallback
        // when the source strongly resembles that environment and does not use
        // the normal Shadertoy iResolution/iTime spelling.
        const bool looksTwigl =
            !source.contains(QRegularExpression("\\biResolution\\b")) &&
            !source.contains(QRegularExpression("\\biTime\\b")) &&
            (source.contains(QRegularExpression("\\bgl_FragCoord\\b")) ||
             source.contains(QRegularExpression("\\bFC\\b"))) &&
            (source.contains(QRegularExpression("\\br\\b")) ||
             source.contains(QRegularExpression("\\bt\\b")));
        if(!looksTwigl)
            return source;

        auto declared = [&](const QString& name) -> bool
        {
            // Parse declarators rather than searching the whole declaration
            // statement. A naive regex would misread `vec2 uv=(...-r)` as a
            // declaration of r simply because r appears in the initializer.
            const QString typePattern =
                "(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?))";

            auto splitTopLevelCommas = [](const QString& text) -> QStringList
            {
                QStringList parts;
                int paren = 0, bracket = 0, brace = 0, begin = 0;
                for(int i = 0; i < text.size(); ++i)
                {
                    const QChar c = text[i];
                    if(c == '(') ++paren;
                    else if(c == ')') --paren;
                    else if(c == '[') ++bracket;
                    else if(c == ']') --bracket;
                    else if(c == '{') ++brace;
                    else if(c == '}') --brace;
                    else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                    {
                        parts << text.mid(begin, i - begin).trimmed();
                        begin = i + 1;
                    }
                }
                parts << text.mid(begin).trimmed();
                return parts;
            };

            const QRegularExpression stmtRe(
                QString("\\b%1\\s+([^;{}\\n]+);").arg(typePattern));
            auto sit = stmtRe.globalMatch(source);
            while(sit.hasNext())
            {
                const QString declarators = sit.next().captured(1);
                for(QString part : splitTopLevelCommas(declarators))
                {
                    // Only inspect the declarator before its initializer.
                    int eq = -1, paren = 0, bracket = 0;
                    for(int i = 0; i < part.size(); ++i)
                    {
                        const QChar c = part[i];
                        if(c == '(') ++paren;
                        else if(c == ')') --paren;
                        else if(c == '[') ++bracket;
                        else if(c == ']') --bracket;
                        else if(c == '=' && paren == 0 && bracket == 0)
                        {
                            eq = i;
                            break;
                        }
                    }
                    if(eq >= 0) part = part.left(eq).trimmed();
                    const auto nm = QRegularExpression("^([A-Za-z_]\\w*)\\s*(?:\\[[^]]*\\])?$").match(part);
                    if(nm.hasMatch() && nm.captured(1) == name)
                        return true;
                }
            }

            // Function parameters are direct `type name` pairs.
            const QRegularExpression paramRe(
                QString("\\b%1\\s+%2\\b(?=\\s*(?:[,)=\\[]))")
                    .arg(typePattern, QRegularExpression::escape(name)));
            if(paramRe.match(source).hasMatch()) return true;

            // A user function with the same short name is not a TwiGL
            // builtin alias.  Protect it just like a variable/parameter.
            const QRegularExpression functionRe(
                QString("\\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?)|[A-Za-z_]\\w*)\\s+%1\\s*\\(")
                    .arg(QRegularExpression::escape(name)));
            if(functionRe.match(source).hasMatch()) return true;

            const QRegularExpression defineRe(
                QString("^\\s*#define\\s+%1\\b").arg(QRegularExpression::escape(name)),
                QRegularExpression::MultilineOption);
            return defineRe.match(source).hasMatch();
        };

        QStringList mapped;
        auto replaceIfUndeclared = [&](const QString& name, const QString& replacement)
        {
            if(declared(name)) return;
            const QRegularExpression re(
                QString("(?<!\\.)\\b%1\\b").arg(QRegularExpression::escape(name)));
            if(!re.match(source).hasMatch()) return;
            source.replace(re, replacement);
            mapped << name;
        };

        replaceIfUndeclared("FC", "gl_FragCoord");
        replaceIfUndeclared("r", "(iResolution.xy)");
        replaceIfUndeclared("t", "iTime");
        replaceIfUndeclared("f", "iFrame");
        // TwiGL's mouse is a vec2; iMouse.xy is the closest BO3/Shadertoy
        // compatibility input.  Backbuffer `b` is intentionally not guessed here
        // because its resource binding differs by conversion target.
        replaceIfUndeclared("m", "(iMouse.xy)");
        replaceIfUndeclared("o", "fragColor");

        mapped.removeDuplicates();
        if(!mapped.isEmpty())
            notes << QString("Mapped TwiGL geek-mode builtin alias(es) to BO3/Shadertoy inputs: %1.")
                         .arg(mapped.join(", "));
        return source;
    }


    QString preExpandGlslFragmentObjectMacros(QString source, QStringList& notes) const
    {
        // Some shader-golf sources intentionally split syntax across object-like
        // macros, for example:
        //
        //   #define hfrac vec2 h){h=fract(h)
        //   #define gthv greaterThan(h,vec2
        //   #define tail );return float(b.x==b.y);}
        //   float checker(hfrac;bvec2 b=gthv(.5)tail
        //
        // A normal token/function converter cannot safely process the individual
        // replacement lists because calls/braces are deliberately unbalanced until
        // the macros are expanded together.  Expand only object-like macros whose
        // replacement text has an unmatched (), [] or {} balance.  Balanced numeric
        // constants and ordinary aliases stay as preprocessor macros.
        struct FragmentMacro
        {
            QString name;
            QString body;
        };

        QMap<QString, FragmentMacro> fragments;
        const QRegularExpression objectDefineRe(
            "^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\\w*)([^\\n]*)$",
            QRegularExpression::MultilineOption);

        auto hasUnbalancedDelimiters = [](const QString& body) -> bool
        {
            int paren = 0, bracket = 0, brace = 0;
            for(const QChar c : body)
            {
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
            }
            return paren != 0 || bracket != 0 || brace != 0;
        };

        auto it = objectDefineRe.globalMatch(source);
        while(it.hasNext())
        {
            const auto m = it.next();
            const QString suffix = m.captured(2);

            // In the C/GLSL preprocessor a macro is function-like only when '('
            // immediately follows the name.  Object-like replacement text does
            // not require separating whitespace, so constructs such as
            // `#define tail);return ...` are valid and must be recognized here.
            if(suffix.startsWith('('))
                continue;

            const QString body = suffix.trimmed();
            if(body.isEmpty() || body.endsWith('\\'))
                continue;
            if(!hasUnbalancedDelimiters(body))
                continue;

            const QString name = m.captured(1);
            fragments.insert(name, {name, body});
        }

        if(fragments.isEmpty())
            return source;

        QStringList expandedNames;
        QStringList lines = source.split('\n', Qt::KeepEmptyParts);
        const QRegularExpression defineNameRe(
            "^\\s*#\\s*define\\s+([A-Za-z_]\\w*)\\b");

        for(QString& line : lines)
        {
            const auto dm = defineNameRe.match(line);
            if(dm.hasMatch() && fragments.contains(dm.captured(1)))
            {
                // Keep a blank physical line so diagnostics remain close to the
                // authored source while removing a macro that would otherwise be
                // unsafe for later token/call conversion.
                line.clear();
                continue;
            }

            bool lineChanged = false;
            for(int pass = 0; pass < 32; ++pass)
            {
                bool passChanged = false;
                for(auto fit = fragments.cbegin(); fit != fragments.cend(); ++fit)
                {
                    const QRegularExpression nameRe(
                        QString("\\b%1\\b").arg(QRegularExpression::escape(fit.key())));
                    if(!nameRe.match(line).hasMatch())
                        continue;

                    line.replace(nameRe, fit.value().body);
                    expandedNames << fit.key();
                    passChanged = true;
                    lineChanged = true;
                }
                if(!passChanged)
                    break;
            }
            Q_UNUSED(lineChanged);
        }

        expandedNames.removeDuplicates();
        if(!expandedNames.isEmpty())
            notes << QString("Pre-expanded syntactic-fragment GLSL object macro(s) before HLSL conversion: %1.")
                         .arg(expandedNames.join(", "));

        return lines.join('\n');
    }

    QString preExpandGlslFxcSensitiveMacros(QString source, QStringList& notes) const
    {
        // FXC's preprocessor is not fully compatible with the macro-heavy GLSL
        // seen in shader-golf code.  Two especially problematic patterns are:
        //   1) top-level macros which generate declarations/functions, and
        //   2) higher-order macros which receive another function-like macro as
        //      an argument (for example perm2(u5cos, p)).
        // Expand only those roots ourselves. Nested ordinary function-like macros
        // are recursively expanded inside the selected replacement text. Macros
        // using token pasting/stringification/variadics are deliberately skipped.
        struct MacroDef
        {
            QString name;
            QStringList params;
            QString body;
            bool expandable = true;
            int sourcePos = -1;
        };

        QMap<QString, MacroDef> defs;
        const QRegularExpression headerRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s*\\(([^)]*)\\)\\s*(.*)$");

        int lineStart = 0;
        while(lineStart < source.size())
        {
            int lineEnd = source.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = source.size();
            const QString firstLine = source.mid(lineStart, lineEnd - lineStart);
            const auto hm = headerRe.match(firstLine);
            if(!hm.hasMatch())
            {
                lineStart = lineEnd < source.size() ? lineEnd + 1 : source.size();
                continue;
            }

            QString body = hm.captured(3);
            int continuationEnd = lineEnd;
            auto stripContinuation = [](QString& text) -> bool
            {
                int i = text.size() - 1;
                while(i >= 0 && (text[i] == ' ' || text[i] == '\t')) --i;
                if(i >= 0 && text[i] == '\\')
                {
                    text.remove(i, 1);
                    return true;
                }
                return false;
            };
            bool continued = stripContinuation(body);
            while(continued && continuationEnd < source.size())
            {
                const int nextStart = continuationEnd + 1;
                int nextEnd = source.indexOf('\n', nextStart);
                if(nextEnd < 0) nextEnd = source.size();
                QString nextLine = source.mid(nextStart, nextEnd - nextStart);
                continued = stripContinuation(nextLine);
                body += "\n" + nextLine;
                continuationEnd = nextEnd;
            }

            QStringList params;
            const QString paramText = hm.captured(2).trimmed();
            bool variadic = paramText.contains("...");
            if(!paramText.isEmpty())
            {
                for(QString part : paramText.split(','))
                    params << part.trimmed();
            }
            const bool hasTokenOps = body.contains("##") ||
                QRegularExpression("(^|[^#])#\\s*[A-Za-z_]\\w*").match(body).hasMatch();
            defs.insert(hm.captured(1), {hm.captured(1), params, body, !variadic && !hasTokenOps, lineStart});
            lineStart = continuationEnd < source.size() ? continuationEnd + 1 : source.size();
        }

        if(defs.isEmpty()) return source;

        auto splitArgs = [](const QString& inside) -> QStringList
        {
            QStringList args;
            int paren = 0, bracket = 0, brace = 0, start = 0;
            for(int i = 0; i < inside.size(); ++i)
            {
                const QChar c = inside[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                {
                    args << inside.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            args << inside.mid(start).trimmed();
            if(args.size() == 1 && args[0].isEmpty()) args.clear();
            return args;
        };

        // Macro parameters are substituted simultaneously by the C/GLSL
        // preprocessor. Never perform sequential QString::replace() calls here:
        // an earlier argument can itself contain an identifier with the same name
        // as a later formal parameter. For example,
        //
        //   #define AD(a,b) su(a,ne(b))
        //   AD(mu(a.b,b.a), mu(a.a,b.b))
        //
        // replacing `a` first and `b` second rewrites the b tokens inside the
        // already-inserted first argument and corrupts it into expressions such as
        // a.mu(...). Tokenize the replacement list once and substitute only the
        // identifiers that belonged to the original macro body.
        auto substituteMacroParams = [](const QString& body,
                                        const QStringList& params,
                                        const QStringList& args) -> QString
        {
            if(params.isEmpty() || params.size() != args.size())
                return body;

            QMap<QString, int> paramIndex;
            for(int i = 0; i < params.size(); ++i)
                paramIndex.insert(params[i], i);

            QString out;
            out.reserve(body.size() * 2);
            int i = 0;
            while(i < body.size())
            {
                const QChar c = body[i];
                if(c.isLetter() || c == '_')
                {
                    const int start = i++;
                    while(i < body.size() && (body[i].isLetterOrNumber() || body[i] == '_'))
                        ++i;
                    const QString token = body.mid(start, i - start);
                    const auto it = paramIndex.constFind(token);
                    if(it != paramIndex.cend())
                        out += args[it.value()];
                    else
                        out += token;
                    continue;
                }
                out += c;
                ++i;
            }
            return out;
        };

        std::function<QString(QString,int,int)> expandText;
        expandText = [&](QString text, int depth, int activeSourcePos) -> QString
        {
            if(depth > 32) return text;
            int from = 0;
            int safety = 0;
            while(from < text.size() && safety++ < 20000)
            {
                int bestPos = -1;
                QString bestName;
                for(auto it = defs.cbegin(); it != defs.cend(); ++it)
                {
                    if(!it.value().expandable || it.value().sourcePos >= activeSourcePos) continue;
                    int p = text.indexOf(it.key(), from, Qt::CaseSensitive);
                    while(p >= 0)
                    {
                        const bool leftOk = p == 0 || !(text[p-1].isLetterOrNumber() || text[p-1] == '_');
                        int after = p + it.key().size();
                        const bool rightOk = after >= text.size() || !(text[after].isLetterOrNumber() || text[after] == '_');
                        while(after < text.size() && text[after].isSpace()) ++after;
                        if(leftOk && rightOk && after < text.size() && text[after] == '(')
                            break;
                        p = text.indexOf(it.key(), p + it.key().size(), Qt::CaseSensitive);
                    }
                    if(p >= 0 && (bestPos < 0 || p < bestPos))
                    {
                        bestPos = p;
                        bestName = it.key();
                    }
                }
                if(bestPos < 0) break;

                int open = bestPos + bestName.size();
                while(open < text.size() && text[open].isSpace()) ++open;
                const int close = glslFindMatchingForward(text, open);
                if(close < 0) { from = open + 1; continue; }

                const MacroDef def = defs.value(bestName);
                const QStringList args = splitArgs(text.mid(open + 1, close - open - 1));
                if(args.size() != def.params.size())
                {
                    from = close + 1;
                    continue;
                }

                QString replacement = substituteMacroParams(def.body, def.params, args);
                replacement = expandText(replacement, depth + 1, activeSourcePos);
                const QString originalInvocation = text.mid(bestPos, close - bestPos + 1);
                if(replacement == originalInvocation)
                {
                    from = close + 1;
                    continue;
                }
                text.replace(bestPos, close - bestPos + 1, replacement);
                from = bestPos;
            }
            return text;
        };

        QString out;
        out.reserve(source.size() * 2);
        int braceDepth = 0;
        bool inDirectiveContinuation = false;
        bool changed = false;
        QStringList roots;

        lineStart = 0;
        while(lineStart < source.size())
        {
            int lineEnd = source.indexOf('\n', lineStart);
            const bool hadNewline = lineEnd >= 0;
            if(lineEnd < 0) lineEnd = source.size();
            QString line = source.mid(lineStart, lineEnd - lineStart);
            const QString trimmed = line.trimmed();
            const bool directive = inDirectiveContinuation || trimmed.startsWith('#');

            if(!directive)
            {
                int from = 0;
                int safety = 0;
                while(from < line.size() && safety++ < 10000)
                {
                    int bestPos = -1;
                    QString bestName;
                    int bestOpen = -1;
                    for(auto it = defs.cbegin(); it != defs.cend(); ++it)
                    {
                        if(!it.value().expandable || it.value().sourcePos >= lineStart) continue;
                        int p = line.indexOf(it.key(), from, Qt::CaseSensitive);
                        while(p >= 0)
                        {
                            const bool leftOk = p == 0 || !(line[p-1].isLetterOrNumber() || line[p-1] == '_');
                            int after = p + it.key().size();
                            const bool rightOk = after >= line.size() || !(line[after].isLetterOrNumber() || line[after] == '_');
                            while(after < line.size() && line[after].isSpace()) ++after;
                            if(leftOk && rightOk && after < line.size() && line[after] == '(')
                            {
                                if(bestPos < 0 || p < bestPos)
                                {
                                    bestPos = p;
                                    bestName = it.key();
                                    bestOpen = after;
                                }
                                break;
                            }
                            p = line.indexOf(it.key(), p + it.key().size(), Qt::CaseSensitive);
                        }
                    }
                    if(bestPos < 0) break;
                    const int close = glslFindMatchingForward(line, bestOpen);
                    if(close < 0) break;
                    const QStringList args = splitArgs(line.mid(bestOpen + 1, close - bestOpen - 1));
                    const MacroDef def = defs.value(bestName);
                    if(args.size() != def.params.size()) { from = close + 1; continue; }

                    bool higherOrder = false;
                    for(const QString& arg : args)
                    {
                        if(defs.contains(arg.trimmed())) { higherOrder = true; break; }
                    }
                    if(braceDepth != 0 && !higherOrder)
                    {
                        from = close + 1;
                        continue;
                    }

                    QString replacement = substituteMacroParams(def.body, def.params, args);
                    replacement = expandText(replacement, 1, lineStart);
                    line.replace(bestPos, close - bestPos + 1, replacement);
                    roots << bestName;
                    changed = true;
                    from = bestPos + replacement.size();
                }
            }

            out += line;
            if(hadNewline) out += '\n';

            if(!directive)
            {
                // Use the original line's structural braces conceptually; any
                // generated declaration bodies are balanced within the expansion.
                for(const QChar c : line)
                {
                    if(c == '{') ++braceDepth;
                    else if(c == '}' && braceDepth > 0) --braceDepth;
                }
            }

            auto endsWithBackslash = [](const QString& text) -> bool
            {
                int i = text.size() - 1;
                while(i >= 0 && (text[i] == ' ' || text[i] == '\t')) --i;
                return i >= 0 && text[i] == '\\';
            };
            if(directive)
                inDirectiveContinuation = endsWithBackslash(line);
            else
                inDirectiveContinuation = false;

            lineStart = hadNewline ? lineEnd + 1 : source.size();
        }

        roots.removeDuplicates();
        if(changed)
            notes << QString("Pre-expanded FXC-sensitive GLSL macro root(s): %1.")
                         .arg(roots.join(", "));
        return out;
    }

    QString inlineGlslSwappedOverloadForwarders(QString source, QStringList& notes) const
    {
        // Legacy FXC can report X3067 for an otherwise exact overload when a
        // macro-heavy GLSL library builds a large overload family around custom
        // structs.  A common GLSL pattern defines the real implementation in one
        // argument order and then defines the commutative order as a trivial
        // forwarding overload:
        //
        //     T op(T a, float b) { return T(op(a.a,b), op(a.b,b)); }
        //     T op(float a, T b) { return op(b,a); }
        //
        // GLSL resolves the second call to the first signature, but FXC 5.0 can
        // consider several scalar/vector promotions equally viable and call the
        // forwarding expression ambiguous.  Inline only these provably-equivalent
        // two-argument swap forwarders using the matching opposite-order function's
        // single return expression.  This preserves source semantics and avoids
        // depending on FXC's weaker overload resolver.
        struct SimpleReturnFunction
        {
            int start = -1;
            int exprStart = -1;
            int exprLength = 0;
            QString returnType;
            QString name;
            QString type1;
            QString param1;
            QString type2;
            QString param2;
            QString expression;
        };

        // Mask preprocessor lines while preserving every source position. Macro
        // definitions often contain function-looking fragments and must not be
        // treated as real function declarations here.
        QString scan = source;
        int lineStart = 0;
        bool continuation = false;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();
            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            const bool directive = continuation || (first < lineEnd && scan[first] == '#');
            if(directive)
            {
                for(int i = lineStart; i < lineEnd; ++i) scan[i] = ' ';
            }

            int tail = lineEnd - 1;
            while(tail >= lineStart && (source[tail] == ' ' || source[tail] == '\t')) --tail;
            continuation = directive && tail >= lineStart && source[tail] == '\\';
            lineStart = lineEnd < scan.size() ? lineEnd + 1 : scan.size();
        }

        const QRegularExpression functionRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(\\s*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*,\\s*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\)\\s*"
            "\\{\\s*return\\s+([^;{}]+);\\s*\\}");

        QVector<SimpleReturnFunction> functions;
        auto it = functionRe.globalMatch(scan);
        while(it.hasNext())
        {
            const auto m = it.next();
            SimpleReturnFunction f;
            f.start = m.capturedStart();
            f.exprStart = m.capturedStart(7);
            f.exprLength = m.capturedLength(7);
            f.returnType = m.captured(1);
            f.name = m.captured(2);
            f.type1 = m.captured(3);
            f.param1 = m.captured(4);
            f.type2 = m.captured(5);
            f.param2 = m.captured(6);
            f.expression = source.mid(f.exprStart, f.exprLength).trimmed();
            functions.push_back(f);
        }

        struct Edit { int start; int length; QString replacement; };
        QVector<Edit> edits;

        for(int i = 0; i < functions.size(); ++i)
        {
            const auto& f = functions[i];
            const QRegularExpression forwardRe(
                QString("^%1\\s*\\(\\s*%2\\s*,\\s*%3\\s*\\)$")
                    .arg(QRegularExpression::escape(f.name),
                         QRegularExpression::escape(f.param2),
                         QRegularExpression::escape(f.param1)));
            if(!forwardRe.match(f.expression).hasMatch())
                continue;

            int counterpart = -1;
            for(int j = i - 1; j >= 0; --j)
            {
                const auto& c = functions[j];
                if(c.returnType != f.returnType || c.name != f.name)
                    continue;
                if(c.type1 != f.type2 || c.type2 != f.type1)
                    continue;
                // Do not inline from another swap-forwarder; we need a concrete
                // implementation expression to avoid creating a forwarding cycle.
                const QRegularExpression cForwardRe(
                    QString("^%1\\s*\\(\\s*%2\\s*,\\s*%3\\s*\\)$")
                        .arg(QRegularExpression::escape(c.name),
                             QRegularExpression::escape(c.param2),
                             QRegularExpression::escape(c.param1)));
                if(cForwardRe.match(c.expression).hasMatch())
                    continue;
                counterpart = j;
                break;
            }
            if(counterpart < 0)
                continue;

            const auto& c = functions[counterpart];

            // Substitute only references to the formal parameters themselves.
            // A regex replacement of every token named `a`/`b` also rewrites
            // struct member names in expressions such as `a.a` and `a.b`, which
            // silently changes the program (for heterogeneous structs it can also
            // trigger truncation warnings).  Tokenize the expression and never
            // substitute an identifier that is the member side of a dot.
            QMap<QString, QString> substitutions;
            substitutions.insert(c.param1, f.param2);
            substitutions.insert(c.param2, f.param1);
            auto substituteExpressionParams = [](const QString& expression,
                                                 const QMap<QString, QString>& replacements) -> QString
            {
                QString out;
                out.reserve(expression.size() * 2);
                int pos = 0;
                while(pos < expression.size())
                {
                    const QChar ch = expression[pos];
                    if(ch.isLetter() || ch == '_')
                    {
                        const int start = pos++;
                        while(pos < expression.size() &&
                              (expression[pos].isLetterOrNumber() || expression[pos] == '_'))
                            ++pos;
                        const QString token = expression.mid(start, pos - start);

                        int previous = start - 1;
                        while(previous >= 0 && expression[previous].isSpace()) --previous;
                        const bool isMemberName = previous >= 0 && expression[previous] == '.';
                        const auto replacementIt = replacements.constFind(token);
                        if(!isMemberName && replacementIt != replacements.cend())
                            out += QString("(%1)").arg(replacementIt.value());
                        else
                            out += token;
                        continue;
                    }
                    out += ch;
                    ++pos;
                }
                return out;
            };

            const QString replacement = substituteExpressionParams(c.expression, substitutions);

            if(replacement != f.expression)
                edits.push_back({f.exprStart, f.exprLength, replacement});
        }

        if(edits.isEmpty())
            return source;

        std::sort(edits.begin(), edits.end(),
                  [](const Edit& a, const Edit& b) { return a.start > b.start; });
        for(const auto& edit : edits)
            source.replace(edit.start, edit.length, edit.replacement);

        notes << QString("Inlined %1 swapped overload forwarder(s) to avoid legacy FXC ambiguous-overload resolution.")
                     .arg(edits.size());
        return source;
    }

    QString inlineGlslExactStructOverloadCalls(QString source, QStringList& notes, int pass = 0) const
    {
        // FXC 5.0 can consider several overloads involving structurally similar
        // user structs equally viable even when GLSL has an exact struct/scalar
        // match.  This shows up in macro-generated AD libraries as calls such as:
        //
        //   DAm2 mu(DAm2 p, vec3 s) {
        //       return DAm2(mu(p.x,s.x), mu(p.y,s.y), mu(p.z,s.z));
        //   }
        //
        // where p.x is exactly w13 and a w13 mu(w13,float) overload exists, yet
        // legacy FXC still reports X3067.  For simple single-return overloads,
        // resolve only calls whose argument types are statically unambiguous from
        // the containing function's parameters/member chains, then inline that
        // exact implementation.  This preserves GLSL overload semantics and keeps
        // the workaround away from calls whose types cannot be proven.
        struct MemberInfo
        {
            QString type;
            QString name;
        };
        struct SimpleFunction
        {
            int exprStart = -1;
            int exprLength = 0;
            QString returnType;
            QString name;
            QString type1;
            QString param1;
            QString type2;
            QString param2;
            QString expression;
            int definitionEnd = -1;
        };
        struct SimpleMultiFunction
        {
            int exprStart = -1;
            int exprLength = 0;
            QString returnType;
            QString name;
            QStringList paramTypes;
            QStringList params;
            QStringList paramDeclarations;
            QString expression;
            int definitionEnd = -1;
        };

        QMap<QString, QMap<QString, QString>> structMembers;
        const QRegularExpression structRe(
            "struct\\s+([A-Za-z_]\\w*)\\s*\\{([^{}]*)\\}\\s*;",
            QRegularExpression::DotMatchesEverythingOption);
        auto structIt = structRe.globalMatch(source);
        while(structIt.hasNext())
        {
            const auto match = structIt.next();
            QMap<QString, QString> members;
            const QStringList statements = match.captured(2).split(';', Qt::SkipEmptyParts);
            for(QString statement : statements)
            {
                statement = statement.trimmed();
                const auto declaration = QRegularExpression(
                    "^(?:const\\s+)?([A-Za-z_]\\w*)\\s+(.+)$").match(statement);
                if(!declaration.hasMatch()) continue;
                const QString type = declaration.captured(1);
                for(QString namePart : declaration.captured(2).split(',', Qt::SkipEmptyParts))
                {
                    namePart = namePart.trimmed();
                    const auto nameMatch = QRegularExpression("^([A-Za-z_]\\w*)$").match(namePart);
                    if(nameMatch.hasMatch())
                        members.insert(nameMatch.captured(1), type);
                }
            }
            if(!members.isEmpty())
                structMembers.insert(match.captured(1), members);
        }
        if(structMembers.isEmpty()) return source;

        // Mask preprocessor directives while preserving source positions so macro
        // fragments are not mistaken for real function definitions.
        QString scan = source;
        int lineStart = 0;
        bool continuation = false;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();
            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            const bool directive = continuation || (first < lineEnd && scan[first] == '#');
            if(directive)
            {
                for(int i = lineStart; i < lineEnd; ++i) scan[i] = ' ';
            }
            int tail = lineEnd - 1;
            while(tail >= lineStart && (source[tail] == ' ' || source[tail] == '\t')) --tail;
            continuation = directive && tail >= lineStart && source[tail] == '\\';
            lineStart = lineEnd < scan.size() ? lineEnd + 1 : scan.size();
        }

        const QRegularExpression functionRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(\\s*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*,\\s*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\)\\s*"
            "\\{\\s*return\\s+([^;{}]+);\\s*\\}");

        QVector<SimpleFunction> functions;
        auto functionIt = functionRe.globalMatch(scan);
        while(functionIt.hasNext())
        {
            const auto match = functionIt.next();
            functions.push_back({
                static_cast<int>(match.capturedStart(7)),
                static_cast<int>(match.capturedLength(7)),
                match.captured(1), match.captured(2),
                match.captured(3), match.captured(4),
                match.captured(5), match.captured(6),
                source.mid(match.capturedStart(7), match.capturedLength(7)).trimmed(),
                static_cast<int>(match.capturedEnd(0))
            });
        }
        QMap<QString, QVector<int>> overloadsByName;
        for(int i = 0; i < functions.size(); ++i)
            overloadsByName[functions[i].name].push_back(i);

        // Keep the established unary/binary records above, and add an arity-generic
        // companion for simple helpers with three or more parameters. Macro-generated
        // GLSL math libraries commonly overload ternary mix/select helpers on several
        // structurally similar user types, which triggers the same legacy FXC X3067
        // ambiguity as their unary and binary helpers.
        QVector<SimpleMultiFunction> multiFunctions;
        QMap<QString, QVector<int>> multiOverloadsByName;
        const QRegularExpression multiFunctionRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(([^;{}()]*)\\)\\s*"
            "\\{\\s*return\\s+([^;{}]+);\\s*\\}");
        const QRegularExpression simpleParameterRe(
            "^(?:(?:const|in|out|inout|lowp|mediump|highp)\\s+)*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)$");
        auto multiFunctionIt = multiFunctionRe.globalMatch(scan);
        while(multiFunctionIt.hasNext())
        {
            const auto match = multiFunctionIt.next();
            const QStringList parameterParts = match.captured(3).split(',', Qt::SkipEmptyParts);
            if(parameterParts.size() < 3) continue;

            QStringList parameterTypes;
            QStringList parameterNames;
            QStringList parameterDeclarations;
            bool validParameters = true;
            for(const QString& parameterPart : parameterParts)
            {
                const auto parameter = simpleParameterRe.match(parameterPart.trimmed());
                if(!parameter.hasMatch())
                {
                    validParameters = false;
                    break;
                }
                parameterTypes << parameter.captured(1);
                parameterNames << parameter.captured(2);
                parameterDeclarations << parameterPart.trimmed();
            }
            if(!validParameters) continue;

            const int index = multiFunctions.size();
            multiFunctions.push_back({
                static_cast<int>(match.capturedStart(4)),
                static_cast<int>(match.capturedLength(4)),
                match.captured(1), match.captured(2),
                parameterTypes, parameterNames, parameterDeclarations,
                source.mid(match.capturedStart(4), match.capturedLength(4)).trimmed(),
                static_cast<int>(match.capturedEnd(0))
            });
            multiOverloadsByName[match.captured(2)].push_back(index);
        }

        // Also record exact unary function signatures.  Nested calls such as
        // `su(ab(p), s)` are common in macro-generated GLSL libraries.  The
        // outer overload can only be resolved if we know that an exact
        // `w11 ab(w11)` call returns w11.  We only need signatures here (not
        // bodies), so accept any unary function body while scanning the same
        // preprocessor-masked source.
        struct UnarySignature
        {
            QString returnType;
            QString name;
            QString paramType;
        };
        struct SimpleUnaryFunction
        {
            int exprStart = -1;
            int exprLength = 0;
            QString returnType;
            QString name;
            QString paramType;
            QString param;
            QString expression;
            int definitionEnd = -1;
        };
        QMap<QString, QVector<UnarySignature>> unarySignaturesByName;
        const QRegularExpression unarySignatureRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(\\s*"
            "([A-Za-z_]\\w*)\\s+[A-Za-z_]\\w*\\s*\\)\\s*\\{");
        auto unaryIt = unarySignatureRe.globalMatch(scan);
        while(unaryIt.hasNext())
        {
            const auto match = unaryIt.next();
            unarySignaturesByName[match.captured(2)].push_back({
                match.captured(1), match.captured(2), match.captured(3)
            });
        }

        // The overload call that FXC finds ambiguous can live inside a simple
        // unary wrapper too (for example `v11 pmod(v11 a) { return
        // mu(su(frfl(c11(...)),c11(...)),c11(...)); }`).  The original exact
        // resolver only visited two-argument function bodies, so even perfect
        // type inference could never rewrite calls inside such unary functions.
        // Reuse SimpleFunction as a lightweight containing-function record with
        // an empty second parameter; binary overload candidates themselves stay
        // in `functions`/`overloadsByName` only.
        QVector<SimpleFunction> containingFunctions = functions;
        QVector<SimpleUnaryFunction> unaryFunctions;
        QMap<QString, QVector<int>> unaryOverloadsByName;
        const QRegularExpression unarySimpleFunctionRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(\\s*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\)\\s*"
            "\\{\\s*return\\s+([^;{}]+);\\s*\\}");
        auto unarySimpleIt = unarySimpleFunctionRe.globalMatch(scan);
        while(unarySimpleIt.hasNext())
        {
            const auto match = unarySimpleIt.next();
            const int unaryIndex = unaryFunctions.size();
            unaryFunctions.push_back({
                static_cast<int>(match.capturedStart(5)),
                static_cast<int>(match.capturedLength(5)),
                match.captured(1), match.captured(2),
                match.captured(3), match.captured(4),
                source.mid(match.capturedStart(5), match.capturedLength(5)).trimmed(),
                static_cast<int>(match.capturedEnd(0))
            });
            unaryOverloadsByName[match.captured(2)].push_back(unaryIndex);
            containingFunctions.push_back({
                static_cast<int>(match.capturedStart(5)),
                static_cast<int>(match.capturedLength(5)),
                match.captured(1), match.captured(2),
                match.captured(3), match.captured(4),
                QString(), QString(),
                source.mid(match.capturedStart(5), match.capturedLength(5)).trimmed()
            });
        }
        if(functions.isEmpty() && unaryFunctions.isEmpty() && multiFunctions.isEmpty()) return source;

        auto canonicalType = [](QString type) -> QString
        {
            type = type.trimmed();
            if(type == "float" || type == "vec1") return "vec1";
            return type;
        };

        auto vectorMemberType = [&](const QString& baseType, const QString& member) -> QString
        {
            const auto vectorMatch = QRegularExpression("^(i|u|b)?vec([234])$").match(baseType);
            if(!vectorMatch.hasMatch() || member.isEmpty()) return {};
            const QString prefix = vectorMatch.captured(1);
            for(const QChar c : member)
            {
                if(!QString("xyzwrgba stpq").remove(' ').contains(c)) return {};
            }
            if(member.size() == 1)
            {
                if(prefix == "i") return "int";
                if(prefix == "u") return "uint";
                if(prefix == "b") return "bool";
                return "vec1";
            }
            return QString("%1vec%2").arg(prefix).arg(member.size());
        };

        auto stripOuterParens = [](QString expression) -> QString
        {
            expression = expression.trimmed();
            bool changed = true;
            while(changed && expression.size() >= 2 && expression.front() == '(' && expression.back() == ')')
            {
                changed = false;
                int depth = 0;
                bool enclosesAll = true;
                for(int i = 0; i < expression.size(); ++i)
                {
                    if(expression[i] == '(') ++depth;
                    else if(expression[i] == ')') --depth;
                    if(depth == 0 && i != expression.size() - 1)
                    {
                        enclosesAll = false;
                        break;
                    }
                }
                if(enclosesAll)
                {
                    expression = expression.mid(1, expression.size() - 2).trimmed();
                    changed = true;
                }
            }
            return expression;
        };

        auto splitArgs = [](const QString& inside) -> QStringList
        {
            QStringList args;
            int paren = 0, bracket = 0, brace = 0, start = 0;
            for(int i = 0; i < inside.size(); ++i)
            {
                const QChar c = inside[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                {
                    args << inside.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            args << inside.mid(start).trimmed();
            if(args.size() == 1 && args[0].isEmpty()) args.clear();
            return args;
        };

        // Textually substituting a function argument into a return expression is
        // only semantics-preserving when both the argument and the substituted
        // expression are free of observable effects. In particular, a user call
        // can mutate global state even though its return type is otherwise easy to
        // infer. Build a conservative, transitive set of such user functions so
        // exact-overload normalization can retain a real call boundary when needed.
        struct UserFunctionEffect
        {
            QString name;
            QString body;
            bool impure = false;
        };
        QVector<UserFunctionEffect> userFunctionEffects;
        const QRegularExpression effectFunctionRe(
            "\\b[A-Za-z_]\\w*\\s+([A-Za-z_]\\w*)\\s*\\(([^;{}]*)\\)\\s*\\{");
        const QRegularExpression mutationRe(
            "(?:\\+\\+|--|(?:<<|>>|[+\\-*/%&|^])=|(?<![=!<>])=(?!=))");
        auto effectFunctionIt = effectFunctionRe.globalMatch(scan);
        while(effectFunctionIt.hasNext())
        {
            const auto function = effectFunctionIt.next();
            const int bodyOpen = function.capturedEnd(0) - 1;
            const int bodyClose = glslFindMatchingForward(scan, bodyOpen);
            if(bodyClose < 0) continue;
            const QString parameters = source.mid(function.capturedStart(2),
                                                  function.capturedLength(2));
            const QString body = source.mid(bodyOpen + 1, bodyClose - bodyOpen - 1);
            const bool outParameter = QRegularExpression("\\b(?:out|inout)\\b")
                                          .match(parameters).hasMatch();
            const bool directMutation = mutationRe.match(body).hasMatch();
            userFunctionEffects.push_back({function.captured(1), body,
                                           outParameter || directMutation});
        }

        QSet<QString> effectfulUserFunctions;
        for(const UserFunctionEffect& function : userFunctionEffects)
        {
            if(function.impure) effectfulUserFunctions.insert(function.name);
        }
        bool discoveredEffectfulCaller = true;
        while(discoveredEffectfulCaller)
        {
            discoveredEffectfulCaller = false;
            for(const UserFunctionEffect& function : userFunctionEffects)
            {
                if(effectfulUserFunctions.contains(function.name)) continue;
                for(const QString& callee : effectfulUserFunctions)
                {
                    const QRegularExpression callRe(
                        QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(callee)));
                    if(!callRe.match(function.body).hasMatch()) continue;
                    effectfulUserFunctions.insert(function.name);
                    discoveredEffectfulCaller = true;
                    break;
                }
            }
        }

        auto expressionMayHaveSideEffects = [&](const QString& expression) -> bool
        {
            if(mutationRe.match(expression).hasMatch()) return true;

            int paren = 0, bracket = 0, brace = 0;
            for(const QChar c : expression)
            {
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                    return true;
            }

            for(const QString& functionName : effectfulUserFunctions)
            {
                const QRegularExpression callRe(
                    QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(functionName)));
                if(callRe.match(expression).hasMatch()) return true;
            }
            return false;
        };

        auto substituteExpressionParams = [](const QString& expression,
                                             const QMap<QString, QString>& replacements) -> QString
        {
            QString out;
            out.reserve(expression.size() * 2);
            int pos = 0;
            while(pos < expression.size())
            {
                const QChar ch = expression[pos];
                if(ch.isLetter() || ch == '_')
                {
                    const int start = pos++;
                    while(pos < expression.size() &&
                          (expression[pos].isLetterOrNumber() || expression[pos] == '_'))
                        ++pos;
                    const QString token = expression.mid(start, pos - start);
                    int previous = start - 1;
                    while(previous >= 0 && expression[previous].isSpace()) --previous;
                    const bool isMemberName = previous >= 0 && expression[previous] == '.';
                    const auto replacementIt = replacements.constFind(token);
                    if(!isMemberName && replacementIt != replacements.cend())
                        out += QString("(%1)").arg(replacementIt.value());
                    else
                        out += token;
                    continue;
                }
                out += ch;
                ++pos;
            }
            return out;
        };

        QMap<QString, QString> objectMacroTypes;
        const QSet<QString> typePreservingUnaryBuiltins = {
            "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan",
            "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "pow", "exp",
            "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor",
            "trunc", "round", "roundEven", "ceil", "fract", "normalize", "dFdx",
            "dFdy", "fwidth"
        };
        int parenthesizedMemberInferenceCount = 0;
        int multiArgumentReturnInferenceCount = 0;

        std::function<QString(QString,const QMap<QString,QString>&)> inferType;
        inferType = [&](QString expression, const QMap<QString, QString>& environment) -> QString
        {
            expression = stripOuterParens(expression);
            if(expression.isEmpty()) return {};
            if(expression[0] == '+' || expression[0] == '-')
                return inferType(expression.mid(1), environment);

            if(QRegularExpression("^(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?[fF]?$").match(expression).hasMatch())
                return "vec1";

            // Infer the result type of simple GLSL arithmetic expressions.  This
            // is deliberately conservative and exists only to carry already-known
            // scalar/vector types through expressions used as overload arguments.
            // Macro-heavy AD libraries frequently hide an otherwise exact custom
            // overload behind a constructor call such as:
            //
            //     c11(a.a / a.b + .5)
            //
            // `a.a`, `a.b`, and `.5` are all scalar, so the constructor argument
            // is provably scalar as well.  Without this step the nested chain
            // c11(...) -> frfl(...) -> su(...) loses its type before FXC sees a
            // large custom-struct overload family and reports X3067.
            auto inferTopLevelArithmetic = [&](const QString& operators) -> QString
            {
                int paren = 0, bracket = 0, brace = 0;
                for(int i = expression.size() - 1; i >= 0; --i)
                {
                    const QChar c = expression[i];
                    if(c == ')') { ++paren; continue; }
                    if(c == '(') { if(paren > 0) --paren; continue; }
                    if(c == ']') { ++bracket; continue; }
                    if(c == '[') { if(bracket > 0) --bracket; continue; }
                    if(c == '}') { ++brace; continue; }
                    if(c == '{') { if(brace > 0) --brace; continue; }
                    if(paren != 0 || bracket != 0 || brace != 0 || !operators.contains(c))
                        continue;

                    // + and - can be unary signs.  Ignore signs at the beginning,
                    // after another operator/delimiter, or inside an exponent.
                    if(c == '+' || c == '-')
                    {
                        int previous = i - 1;
                        while(previous >= 0 && expression[previous].isSpace()) --previous;
                        if(previous < 0) continue;
                        const QChar p = expression[previous];
                        if(p == 'e' || p == 'E' || p == '(' || p == '[' || p == '{' ||
                           p == ',' || p == '+' || p == '-' || p == '*' || p == '/' || p == '%')
                            continue;
                    }

                    const QString left = expression.left(i).trimmed();
                    const QString right = expression.mid(i + 1).trimmed();
                    if(left.isEmpty() || right.isEmpty()) continue;

                    const QString leftType = canonicalType(inferType(left, environment));
                    const QString rightType = canonicalType(inferType(right, environment));
                    if(leftType.isEmpty() || rightType.isEmpty()) continue;
                    if(leftType == rightType) return leftType;

                    // GLSL permits scalar/vector arithmetic.  Preserve the vector
                    // side when the other operand is a proven float scalar.  Do
                    // not guess through custom structs or matrices here.
                    const auto isBuiltinVector = [](const QString& type) -> bool
                    {
                        return QRegularExpression("^(?:[iub]?vec[234])$").match(type).hasMatch();
                    };
                    if(leftType == "vec1" && isBuiltinVector(rightType)) return rightType;
                    if(rightType == "vec1" && isBuiltinVector(leftType)) return leftType;
                }
                return {};
            };

            // Respect normal arithmetic precedence for the split point.  Type
            // propagation does not depend on value evaluation, but choosing the
            // lowest-precedence top-level operator first keeps recursion aligned
            // with the authored expression and avoids treating unary signs as
            // binary operators.
            QString arithmeticType = inferTopLevelArithmetic("+-");
            if(arithmeticType.isEmpty()) arithmeticType = inferTopLevelArithmetic("*/%");
            if(!arithmeticType.isEmpty()) return arithmeticType;

            const auto ctor = QRegularExpression("^([A-Za-z_]\\w*)\\s*\\(").match(expression);
            if(ctor.hasMatch())
            {
                const QString ctorType = ctor.captured(1);
                if(structMembers.contains(ctorType) ||
                   QRegularExpression("^(?:vec[1-4]|ivec[2-4]|uvec[2-4]|bvec[2-4]|float|int|uint|bool)$").match(ctorType).hasMatch())
                    return canonicalType(ctorType);

                // Infer the return type of an exact unary function call.  Keep
                // this deliberately strict: the call must occupy the entire
                // expression, its argument type must itself be provable, and
                // exactly one unary signature must match that type.
                const int open = expression.indexOf('(', ctor.capturedStart(1) + ctor.capturedLength(1));
                if(open >= 0)
                {
                    const int close = glslFindMatchingForward(expression, open);
                    if(close == expression.size() - 1)
                    {
                        const QStringList callArgs = splitArgs(expression.mid(open + 1, close - open - 1));
                        if(callArgs.size() == 1)
                        {
                            const QString argumentType = canonicalType(inferType(callArgs[0], environment));
                            if(!argumentType.isEmpty())
                            {
                                if(typePreservingUnaryBuiltins.contains(ctorType))
                                    return argumentType;

                                QString matchedReturnType;
                                bool ambiguousReturn = false;
                                const auto signatures = unarySignaturesByName.constFind(ctorType);
                                if(signatures != unarySignaturesByName.cend())
                                {
                                    for(const UnarySignature& signature : signatures.value())
                                    {
                                        if(canonicalType(signature.paramType) != argumentType)
                                            continue;
                                        const QString candidateReturn = canonicalType(signature.returnType);
                                        if(matchedReturnType.isEmpty())
                                            matchedReturnType = candidateReturn;
                                        else if(matchedReturnType != candidateReturn)
                                        {
                                            ambiguousReturn = true;
                                            break;
                                        }
                                    }
                                }
                                if(!ambiguousReturn && !matchedReturnType.isEmpty())
                                    return matchedReturnType;
                            }
                        }
                        else if(callArgs.size() == 2)
                        {
                            // Constructor-like helper functions are common in macro-heavy
                            // GLSL libraries, e.g. `c11(.5,.0)` returning a custom v11
                            // struct.  Nested expressions such as
                            // `su(frfl(c11(...)), c11(...))` can only be resolved if the
                            // type inference follows that exact binary helper call first.
                            // Reuse the already-collected simple two-argument signatures
                            // and accept only one exact parameter-type match.
                            const QString argumentType1 = canonicalType(inferType(callArgs[0], environment));
                            const QString argumentType2 = canonicalType(inferType(callArgs[1], environment));
                            if(!argumentType1.isEmpty() && !argumentType2.isEmpty())
                            {
                                QString matchedReturnType;
                                bool ambiguousReturn = false;
                                const auto signatures = overloadsByName.constFind(ctorType);
                                if(signatures != overloadsByName.cend())
                                {
                                    for(const int index : signatures.value())
                                    {
                                        const SimpleFunction& signature = functions[index];
                                        if(canonicalType(signature.type1) != argumentType1 ||
                                           canonicalType(signature.type2) != argumentType2)
                                            continue;
                                        const QString candidateReturn = canonicalType(signature.returnType);
                                        if(matchedReturnType.isEmpty())
                                            matchedReturnType = candidateReturn;
                                        else
                                        {
                                            ambiguousReturn = true;
                                            break;
                                        }
                                    }
                                }
                                if(!ambiguousReturn && !matchedReturnType.isEmpty())
                                    return matchedReturnType;
                            }
                        }
                        else if(callArgs.size() >= 3)
                        {
                            QStringList argumentTypes;
                            bool allTypesKnown = true;
                            for(const QString& callArg : callArgs)
                            {
                                const QString argumentType = canonicalType(inferType(callArg, environment));
                                if(argumentType.isEmpty())
                                {
                                    allTypesKnown = false;
                                    break;
                                }
                                argumentTypes << argumentType;
                            }

                            if(allTypesKnown)
                            {
                                QString matchedReturnType;
                                bool ambiguousReturn = false;
                                const auto signatures = multiOverloadsByName.constFind(ctorType);
                                if(signatures != multiOverloadsByName.cend())
                                {
                                    for(const int index : signatures.value())
                                    {
                                        const SimpleMultiFunction& signature = multiFunctions[index];
                                        if(signature.paramTypes.size() != argumentTypes.size()) continue;
                                        bool exactMatch = true;
                                        for(int i = 0; i < argumentTypes.size(); ++i)
                                        {
                                            if(canonicalType(signature.paramTypes[i]) != argumentTypes[i])
                                            {
                                                exactMatch = false;
                                                break;
                                            }
                                        }
                                        if(!exactMatch) continue;

                                        const QString candidateReturn = canonicalType(signature.returnType);
                                        if(matchedReturnType.isEmpty())
                                            matchedReturnType = candidateReturn;
                                        else if(matchedReturnType != candidateReturn)
                                        {
                                            ambiguousReturn = true;
                                            break;
                                        }
                                    }
                                }
                                if(!ambiguousReturn && !matchedReturnType.isEmpty())
                                {
                                    ++multiArgumentReturnInferenceCount;
                                    return matchedReturnType;
                                }
                            }
                        }
                    }
                }
            }

            // Safe parameter substitution deliberately wraps arguments in
            // parentheses. Preserve member-chain typing through equivalent forms
            // such as `(p).b` and `(makeValue(...)).field`; otherwise a pass can
            // lose the very type information produced by an earlier pass.
            if(expression.startsWith('('))
            {
                const int primaryClose = glslFindMatchingForward(expression, 0);
                if(primaryClose > 0 && primaryClose < expression.size() - 1)
                {
                    const QString suffix = expression.mid(primaryClose + 1);
                    if(QRegularExpression("^(?:\\s*\\.\\s*[A-Za-z_]\\w*)+$")
                           .match(suffix).hasMatch())
                    {
                        QString type = canonicalType(
                            inferType(expression.mid(1, primaryClose - 1), environment));
                        auto memberIt = QRegularExpression("\\.\\s*([A-Za-z_]\\w*)")
                                            .globalMatch(suffix);
                        while(!type.isEmpty() && memberIt.hasNext())
                        {
                            const QString member = memberIt.next().captured(1);
                            const auto structIt = structMembers.constFind(type);
                            if(structIt != structMembers.cend())
                                type = canonicalType(structIt.value().value(member));
                            else
                                type = canonicalType(vectorMemberType(type, member));
                        }
                        if(!type.isEmpty())
                        {
                            ++parenthesizedMemberInferenceCount;
                            return type;
                        }
                    }
                }
            }

            const auto chain = QRegularExpression("^([A-Za-z_]\\w*)((?:\\s*\\.\\s*[A-Za-z_]\\w*)*)$").match(expression);
            if(!chain.hasMatch()) return {};
            QString type = environment.value(chain.captured(1));
            if(type.isEmpty()) type = objectMacroTypes.value(chain.captured(1));
            if(type.isEmpty()) return {};
            type = canonicalType(type);

            const QString suffix = chain.captured(2);
            auto memberIt = QRegularExpression("\\.\\s*([A-Za-z_]\\w*)").globalMatch(suffix);
            while(memberIt.hasNext())
            {
                const QString member = memberIt.next().captured(1);
                const auto structIt = structMembers.constFind(type);
                if(structIt != structMembers.cend())
                {
                    type = canonicalType(structIt.value().value(member));
                    if(type.isEmpty()) return {};
                    continue;
                }
                const QString memberType = vectorMemberType(type, member);
                if(memberType.isEmpty()) return {};
                type = canonicalType(memberType);
            }
            return canonicalType(type);
        };

        // Object-like value macros participate in overload expressions just like
        // literals, but their identifiers are invisible to the local-variable type
        // environment. Infer only macros whose complete replacement expression has
        // a provable type, and iterate so one value macro may depend on an earlier
        // value macro. Function-like macros are excluded by requiring whitespace
        // between the macro name and replacement text.
        QVector<QPair<QString, QString>> objectMacroDefinitions;
        const QRegularExpression objectMacroRe(
            "^\\s*#define[ \\t]+([A-Za-z_]\\w*)[ \\t]+([^\\n]+)$",
            QRegularExpression::MultilineOption);
        auto objectMacroIt = objectMacroRe.globalMatch(source);
        while(objectMacroIt.hasNext())
        {
            const auto macro = objectMacroIt.next();
            const QString replacement = macro.captured(2).trimmed();
            if(!replacement.isEmpty() && !replacement.endsWith('\\'))
                objectMacroDefinitions.push_back({macro.captured(1), replacement});
        }

        for(int macroPass = 0; macroPass < objectMacroDefinitions.size(); ++macroPass)
        {
            bool changed = false;
            for(const auto& definition : objectMacroDefinitions)
            {
                if(objectMacroTypes.contains(definition.first)) continue;
                const QString inferred = canonicalType(inferType(definition.second, {}));
                if(inferred.isEmpty()) continue;
                objectMacroTypes.insert(definition.first, inferred);
                changed = true;
            }
            if(!changed) break;
        }
        if(!objectMacroTypes.isEmpty())
            notes << QString("Inferred %1 object-like GLSL macro value type(s) for exact overload resolution.")
                         .arg(objectMacroTypes.size());

        auto resolveExactStructOverload = [&](const QString& name,
                                              const QStringList& args,
                                              const QMap<QString, QString>& environment) -> int
        {
            if(name.startsWith("__bo3_exact_call_")) return -1;
            if(args.size() != 2) return -1;
            const auto overloadIt = overloadsByName.constFind(name);
            if(overloadIt == overloadsByName.cend()) return -1;

            const QString argType1 = canonicalType(inferType(args[0], environment));
            const QString argType2 = canonicalType(inferType(args[1], environment));
            if(argType1.isEmpty() || argType2.isEmpty()) return -1;

            int candidateIndex = -1;
            for(const int index : overloadIt.value())
            {
                const SimpleFunction& candidate = functions[index];
                if(canonicalType(candidate.type1) != argType1 ||
                   canonicalType(candidate.type2) != argType2)
                    continue;
                if(candidateIndex >= 0) return -1;
                candidateIndex = index;
            }
            if(candidateIndex < 0) return -1;

            const SimpleFunction& candidate = functions[candidateIndex];
            if(!structMembers.contains(candidate.type1) &&
               !structMembers.contains(candidate.type2))
                return -1;
            return candidateIndex;
        };

        auto resolveExactUnaryStructOverload = [&](const QString& name,
                                                   const QStringList& args,
                                                   const QMap<QString, QString>& environment) -> int
        {
            if(name.startsWith("__bo3_exact_call_")) return -1;
            if(args.size() != 1) return -1;
            const auto overloadIt = unaryOverloadsByName.constFind(name);
            if(overloadIt == unaryOverloadsByName.cend()) return -1;

            const QString argType = canonicalType(inferType(args[0], environment));
            if(argType.isEmpty() || !structMembers.contains(argType)) return -1;

            int candidateIndex = -1;
            for(const int index : overloadIt.value())
            {
                const SimpleUnaryFunction& candidate = unaryFunctions[index];
                if(canonicalType(candidate.paramType) != argType) continue;
                if(candidateIndex >= 0) return -1;
                candidateIndex = index;
            }
            return candidateIndex;
        };

        auto resolveExactMultiStructOverload = [&](const QString& name,
                                                   const QStringList& args,
                                                   const QMap<QString, QString>& environment) -> int
        {
            if(name.startsWith("__bo3_exact_call_")) return -1;
            if(args.size() < 3) return -1;
            const auto overloadIt = multiOverloadsByName.constFind(name);
            if(overloadIt == multiOverloadsByName.cend()) return -1;

            QStringList argumentTypes;
            for(const QString& arg : args)
            {
                const QString type = canonicalType(inferType(arg, environment));
                if(type.isEmpty()) return -1;
                argumentTypes << type;
            }

            int candidateIndex = -1;
            for(const int index : overloadIt.value())
            {
                const SimpleMultiFunction& candidate = multiFunctions[index];
                if(candidate.paramTypes.size() != argumentTypes.size()) continue;
                bool exactMatch = true;
                bool hasStructParameter = false;
                for(int i = 0; i < argumentTypes.size(); ++i)
                {
                    const QString parameterType = canonicalType(candidate.paramTypes[i]);
                    if(parameterType != argumentTypes[i])
                    {
                        exactMatch = false;
                        break;
                    }
                    if(structMembers.contains(parameterType)) hasStructParameter = true;
                }
                if(!exactMatch || !hasStructParameter) continue;
                if(candidateIndex >= 0) return -1;
                candidateIndex = index;
            }
            return candidateIndex;
        };

        struct Edit { int start; int length; QString replacement; };
        QVector<Edit> edits;
        int inlineCount = 0;
        int functionBodyInlineCount = 0;
        int unaryInlineCount = 0;
        int multiArgumentInlineCount = 0;
        int preservedCallBoundaryCount = 0;
        int structValuedMacroExpansionCount = 0;
        int transitiveSensitiveMacroCount = 0;
        int insideOutDeferredCount = 0;
        QMap<int, QString> binaryCallBoundaryHelpers;
        QMap<int, QString> unaryCallBoundaryHelpers;
        QMap<int, QString> multiCallBoundaryHelpers;

        auto uniqueCallBoundaryHelperName = [&](const QString& originalName,
                                                const QString& kind,
                                                int candidateIndex) -> QString
        {
            QString helper = QString("__bo3_exact_call_%1_%2_%3")
                                 .arg(originalName)
                                 .arg(kind)
                                 .arg(candidateIndex);
            int suffix = 1;
            while(QRegularExpression(QString("\\b%1\\b")
                                         .arg(QRegularExpression::escape(helper)))
                      .match(source).hasMatch())
                helper = QString("__bo3_exact_call_%1_%2_%3_%4")
                             .arg(originalName)
                             .arg(kind)
                             .arg(candidateIndex)
                             .arg(++suffix);
            return helper;
        };

        auto binaryCallBoundaryHelper = [&](int candidateIndex) -> QString
        {
            const auto existing = binaryCallBoundaryHelpers.constFind(candidateIndex);
            if(existing != binaryCallBoundaryHelpers.cend()) return existing.value();
            const QString helper = uniqueCallBoundaryHelperName(
                functions[candidateIndex].name, "b", candidateIndex);
            binaryCallBoundaryHelpers.insert(candidateIndex, helper);
            return helper;
        };

        auto unaryCallBoundaryHelper = [&](int candidateIndex) -> QString
        {
            const auto existing = unaryCallBoundaryHelpers.constFind(candidateIndex);
            if(existing != unaryCallBoundaryHelpers.cend()) return existing.value();
            const QString helper = uniqueCallBoundaryHelperName(
                unaryFunctions[candidateIndex].name, "u", candidateIndex);
            unaryCallBoundaryHelpers.insert(candidateIndex, helper);
            return helper;
        };

        auto multiCallBoundaryHelper = [&](int candidateIndex) -> QString
        {
            const auto existing = multiCallBoundaryHelpers.constFind(candidateIndex);
            if(existing != multiCallBoundaryHelpers.cend()) return existing.value();
            const QString helper = uniqueCallBoundaryHelperName(
                multiFunctions[candidateIndex].name, "m", candidateIndex);
            multiCallBoundaryHelpers.insert(candidateIndex, helper);
            return helper;
        };

        auto needsPreservedCallBoundary = [&](const QString& candidateName,
                                              const QString& candidateExpression,
                                              const QStringList& args) -> bool
        {
            if(effectfulUserFunctions.contains(candidateName)) return true;
            if(expressionMayHaveSideEffects(candidateExpression)) return true;
            for(const QString& arg : args)
            {
                if(expressionMayHaveSideEffects(arg)) return true;
            }
            return false;
        };
        const bool traceOverloadResolution = qEnvironmentVariableIsSet("BO3_TRACE_GLSL_OVERLOADS");
        QSet<QString> tracedUnresolvedCalls;
        auto traceUnresolvedCall = [&](const QString& name,
                                       const QStringList& args,
                                       const QMap<QString, QString>& environment)
        {
            if(!traceOverloadResolution || tracedUnresolvedCalls.size() >= 32) return;
            QStringList types;
            bool hasStructArgument = false;
            for(const QString& arg : args)
            {
                QString type = canonicalType(inferType(arg, environment));
                if(type.isEmpty()) type = "?";
                if(structMembers.contains(type)) hasStructArgument = true;
                types << type;
            }
            if(!hasStructArgument) return;
            const QString trace = QString("Overload trace: unresolved %1(%2)")
                                      .arg(name, types.join(", "));
            if(tracedUnresolvedCalls.contains(trace)) return;
            tracedUnresolvedCalls.insert(trace);
            notes << trace;
        };

        for(const SimpleFunction& containing : containingFunctions)
        {
            QMap<QString, QString> environment;
            environment.insert(containing.param1, canonicalType(containing.type1));
            if(!containing.param2.isEmpty())
                environment.insert(containing.param2, canonicalType(containing.type2));

            QString expression = containing.expression;
            struct LocalEdit { int start; int length; QString replacement; };
            struct ResolvableCall
            {
                int start = -1;
                int length = 0;
                int candidateIndex = -1;
                bool unary = false;
                QStringList args;
            };
            QVector<ResolvableCall> resolvableCalls;

            // Discover every provably exact custom-struct overload call in the
            // return expression, including nested calls.  Older revisions jumped
            // directly to the closing ')' after recognizing an outer call.  That
            // meant an expression such as
            //
            //   mu(su(frfl(c11(...)), c11(...)), c11(...))
            //
            // could inline the outer mu(...) first and bury the still-ambiguous
            // su(...) inside generated member accesses like `(su(...)).a`.  FXC
            // would then see the inner ambiguity before our next whole-source
            // pass had a clean call expression to rewrite.  Collect the complete
            // call tree first, then apply only the deepest non-overlapping calls;
            // parent calls are naturally handled by the next bounded pass.
            int pos = 0;
            while(pos < expression.size())
            {
                if(!(expression[pos].isLetter() || expression[pos] == '_'))
                {
                    ++pos;
                    continue;
                }
                const int nameStart = pos++;
                while(pos < expression.size() &&
                      (expression[pos].isLetterOrNumber() || expression[pos] == '_'))
                    ++pos;
                const QString name = expression.mid(nameStart, pos - nameStart);
                const bool hasBinaryOverloads = overloadsByName.contains(name);
                const bool hasUnaryOverloads = unaryOverloadsByName.contains(name);
                if(!hasBinaryOverloads && !hasUnaryOverloads) continue;

                int open = pos;
                while(open < expression.size() && expression[open].isSpace()) ++open;
                if(open >= expression.size() || expression[open] != '(') continue;
                const int close = glslFindMatchingForward(expression, open);
                if(close < 0) { pos = open + 1; continue; }
                const QStringList args = splitArgs(expression.mid(open + 1, close - open - 1));

                // Keep scanning inside this call even when the current call is not
                // itself eligible.  A nested exact call may still need lowering.
                pos = open + 1;
                const bool unaryCall = args.size() == 1;
                const int candidateIndex = unaryCall
                    ? resolveExactUnaryStructOverload(name, args, environment)
                    : resolveExactStructOverload(name, args, environment);
                if(candidateIndex < 0)
                {
                    traceUnresolvedCall(name, args, environment);
                    continue;
                }

                // Do not inline a recursive self-call with the same exact signature.
                if(unaryCall)
                {
                    const SimpleUnaryFunction& candidate = unaryFunctions[candidateIndex];
                    if(candidate.name == containing.name &&
                       canonicalType(candidate.paramType) == canonicalType(containing.type1) &&
                       containing.param2.isEmpty())
                        continue;
                }
                else
                {
                    const SimpleFunction& candidate = functions[candidateIndex];
                    if(candidate.name == containing.name &&
                       canonicalType(candidate.type1) == canonicalType(containing.type1) &&
                       canonicalType(candidate.type2) == canonicalType(containing.type2))
                        continue;
                }

                resolvableCalls.push_back({nameStart, close - nameStart + 1, candidateIndex, unaryCall, args});
            }

            if(resolvableCalls.isEmpty()) continue;

            // A nested call always starts after its parent.  Visiting calls from
            // right to left therefore encounters children before ancestors.  Keep
            // all disjoint/deepest calls and defer any overlapping parent call to
            // the next exact-overload pass.  This gives deterministic inside-out
            // normalization without trying to apply overlapping source edits.
            std::sort(resolvableCalls.begin(), resolvableCalls.end(),
                      [](const ResolvableCall& a, const ResolvableCall& b)
                      {
                          if(a.start != b.start) return a.start > b.start;
                          return a.length < b.length;
                      });

            QVector<LocalEdit> localEdits;
            for(const ResolvableCall& call : resolvableCalls)
            {
                const int callEnd = call.start + call.length;
                bool overlapsSelected = false;
                for(const LocalEdit& selected : localEdits)
                {
                    const int selectedEnd = selected.start + selected.length;
                    if(call.start < selectedEnd && selected.start < callEnd)
                    {
                        overlapsSelected = true;
                        break;
                    }
                }
                if(overlapsSelected)
                {
                    ++insideOutDeferredCount;
                    continue;
                }

                QMap<QString, QString> replacements;
                QString candidateExpression;
                QString candidateName;
                if(call.unary)
                {
                    const SimpleUnaryFunction& candidate = unaryFunctions[call.candidateIndex];
                    replacements.insert(candidate.param, call.args[0]);
                    candidateExpression = candidate.expression;
                    candidateName = candidate.name;
                }
                else
                {
                    const SimpleFunction& candidate = functions[call.candidateIndex];
                    replacements.insert(candidate.param1, call.args[0]);
                    replacements.insert(candidate.param2, call.args[1]);
                    candidateExpression = candidate.expression;
                    candidateName = candidate.name;
                }
                QString replacement;
                if(needsPreservedCallBoundary(candidateName, candidateExpression, call.args))
                {
                    const QString callBoundaryHelper = call.unary
                        ? unaryCallBoundaryHelper(call.candidateIndex)
                        : binaryCallBoundaryHelper(call.candidateIndex);
                    replacement = QString("%1(%2)")
                                      .arg(callBoundaryHelper, call.args.join(", "));
                    ++preservedCallBoundaryCount;
                }
                else
                {
                    replacement = QString("(%1)").arg(
                        substituteExpressionParams(candidateExpression, replacements));
                    if(call.unary) ++unaryInlineCount;
                }
                localEdits.push_back({call.start, call.length, replacement});
            }

            if(localEdits.isEmpty()) continue;
            std::sort(localEdits.begin(), localEdits.end(),
                      [](const LocalEdit& a, const LocalEdit& b) { return a.start > b.start; });
            for(const LocalEdit& edit : localEdits)
            {
                expression.replace(edit.start, edit.length, edit.replacement);
                if(!edit.replacement.startsWith("__bo3_exact_call_")) ++inlineCount;
            }
            if(expression != containing.expression)
                edits.push_back({containing.exprStart, containing.exprLength, expression});
        }

        // A function-like macro can hide an overloaded user-struct call from this
        // pass until FXC expands it. Record safe, single-line macros whose replacement
        // invokes an overload family that includes a custom struct. At a call site we
        // expand only when at least one argument is provably struct-valued, keeping
        // unrelated numeric convenience macros intact.
        QSet<QString> customOverloadNames;
        for(const SimpleFunction& function : functions)
        {
            if(structMembers.contains(function.type1) || structMembers.contains(function.type2))
                customOverloadNames.insert(function.name);
        }
        for(const SimpleUnaryFunction& function : unaryFunctions)
        {
            if(structMembers.contains(function.paramType))
                customOverloadNames.insert(function.name);
        }
        for(const SimpleMultiFunction& function : multiFunctions)
        {
            for(const QString& parameterType : function.paramTypes)
            {
                if(!structMembers.contains(parameterType)) continue;
                customOverloadNames.insert(function.name);
                break;
            }
        }

        struct SensitiveFunctionMacro
        {
            QStringList params;
            QString body;
        };
        QMap<QString, SensitiveFunctionMacro> allFunctionMacros;
        const QRegularExpression functionMacroRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\(([^)]*)\\)\\s*(.*)$",
            QRegularExpression::MultilineOption);
        auto functionMacroIt = functionMacroRe.globalMatch(source);
        while(functionMacroIt.hasNext())
        {
            const auto macro = functionMacroIt.next();
            const QString body = macro.captured(3).trimmed();
            const QString parameterText = macro.captured(2).trimmed();
            if(body.isEmpty() || body.endsWith('\\') || parameterText.contains("...") ||
               body.contains("##") ||
               QRegularExpression("(^|[^#])#\\s*[A-Za-z_]\\w*").match(body).hasMatch())
                continue;

            QStringList params;
            if(!parameterText.isEmpty())
            {
                for(QString parameter : parameterText.split(','))
                    params << parameter.trimmed();
            }
            allFunctionMacros.insert(macro.captured(1), {params, body});
        }

        auto bodyCallsAny = [](const QString& body, const QSet<QString>& names) -> bool
        {
            for(const QString& name : names)
            {
                const QRegularExpression callRe(
                    QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(name)));
                if(callRe.match(body).hasMatch()) return true;
            }
            return false;
        };

        QSet<QString> sensitiveMacroNames;
        for(auto macroIt = allFunctionMacros.cbegin(); macroIt != allFunctionMacros.cend(); ++macroIt)
        {
            if(bodyCallsAny(macroIt.value().body, customOverloadNames))
                sensitiveMacroNames.insert(macroIt.key());
        }

        // Macro wrappers can hide the overload family by one or more expansion
        // layers (for example WRAP(x) -> DIRECT(x) -> overloaded(x)).  Compute
        // the transitive set once, then retain the existing call-site guard that
        // requires a provably struct-valued argument before expanding anything.
        bool discoveredWrapper = true;
        while(discoveredWrapper)
        {
            discoveredWrapper = false;
            for(auto macroIt = allFunctionMacros.cbegin(); macroIt != allFunctionMacros.cend(); ++macroIt)
            {
                if(sensitiveMacroNames.contains(macroIt.key())) continue;
                if(!bodyCallsAny(macroIt.value().body, sensitiveMacroNames)) continue;
                sensitiveMacroNames.insert(macroIt.key());
                ++transitiveSensitiveMacroCount;
                discoveredWrapper = true;
            }
        }

        QMap<QString, SensitiveFunctionMacro> sensitiveFunctionMacros;
        for(const QString& macroName : sensitiveMacroNames)
        {
            const auto macroIt = allFunctionMacros.constFind(macroName);
            if(macroIt != allFunctionMacros.cend())
                sensitiveFunctionMacros.insert(macroName, macroIt.value());
        }

        auto expandSensitiveMacro = [](const SensitiveFunctionMacro& macro,
                                       const QStringList& args) -> QString
        {
            if(macro.params.size() != args.size()) return {};
            QMap<QString, int> parameterIndexes;
            for(int i = 0; i < macro.params.size(); ++i)
                parameterIndexes.insert(macro.params[i], i);

            QString expanded;
            expanded.reserve(macro.body.size() * 2);
            int pos = 0;
            while(pos < macro.body.size())
            {
                if(macro.body[pos].isLetter() || macro.body[pos] == '_')
                {
                    const int start = pos++;
                    while(pos < macro.body.size() &&
                          (macro.body[pos].isLetterOrNumber() || macro.body[pos] == '_'))
                        ++pos;
                    const QString token = macro.body.mid(start, pos - start);
                    const auto parameterIt = parameterIndexes.constFind(token);
                    if(parameterIt != parameterIndexes.cend())
                        expanded += QString("(%1)").arg(args[parameterIt.value()]);
                    else
                        expanded += token;
                    continue;
                }
                expanded += macro.body[pos++];
            }
            return expanded;
        };

        // The original exact resolver was intentionally limited to functions whose
        // entire body was a single return statement.  The same FXC ambiguity also
        // occurs in normal multi-statement functions, especially when a local custom
        // struct is updated by a later assignment.  Scan complete function bodies and
        // build a conservative type environment from parameters and local declarations.
        // A name is usable only when every declaration seen in that function agrees on
        // its type, so shadowing with a different type disables inference instead of
        // risking a semantic rewrite.
        const QRegularExpression generalFunctionRe(
            "\\b([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(([^;{}]*)\\)\\s*\\{");
        const QRegularExpression parameterRe(
            "^(?:(?:const|in|out|inout|lowp|mediump|highp)\\s+)*"
            "([A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)(?:\\s*\\[[^\\]]*\\])?$");
        const QRegularExpression localDeclarationRe(
            "\\b(?:(?:const|static)\\s+)*([A-Za-z_]\\w*)\\s+([^;{}]+);");
        const QRegularExpression builtinTypeRe(
            "^(?:vec[1-4]|ivec[2-4]|uvec[2-4]|bvec[2-4]|"
            "mat[234](?:x[234])?|float|int|uint|bool)$");

        auto generalIt = generalFunctionRe.globalMatch(scan);
        while(generalIt.hasNext())
        {
            const auto functionMatch = generalIt.next();
            const int bodyOpen = functionMatch.capturedEnd(0) - 1;
            const int bodyClose = glslFindMatchingForward(scan, bodyOpen);
            if(bodyOpen < 0 || bodyClose < 0) continue;
            const int bodyStart = bodyOpen + 1;

            // A single-return function already rewritten above has a pending edit
            // covering its expression.  Do not create overlapping source edits for
            // that body; the recursive pass will revisit its rewritten source.
            bool hasExistingBodyEdit = false;
            for(const Edit& edit : edits)
            {
                if(edit.start >= bodyStart && edit.start < bodyClose)
                {
                    hasExistingBodyEdit = true;
                    break;
                }
            }
            if(hasExistingBodyEdit) continue;

            QMap<QString, QString> environment;
            QSet<QString> ambiguousNames;
            auto addEnvironmentType = [&](const QString& name, const QString& type)
            {
                if(name.isEmpty() || type.isEmpty() || ambiguousNames.contains(name)) return;
                const QString canonical = canonicalType(type);
                const auto existing = environment.constFind(name);
                if(existing != environment.cend() && existing.value() != canonical)
                {
                    environment.remove(name);
                    ambiguousNames.insert(name);
                    return;
                }
                environment.insert(name, canonical);
            };

            QString currentType1;
            QString currentType2;
            QStringList currentParameterTypes;
            const QStringList parameters = splitArgs(functionMatch.captured(3));
            for(int parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
            {
                const auto parameter = parameterRe.match(parameters[parameterIndex].trimmed());
                if(!parameter.hasMatch()) continue;
                const QString type = parameter.captured(1);
                const QString name = parameter.captured(2);
                addEnvironmentType(name, type);
                currentParameterTypes << canonicalType(type);
                if(parameterIndex == 0) currentType1 = canonicalType(type);
                else if(parameterIndex == 1) currentType2 = canonicalType(type);
            }

            const QString bodyScan = scan.mid(bodyStart, bodyClose - bodyStart);
            auto declarationIt = localDeclarationRe.globalMatch(bodyScan);
            while(declarationIt.hasNext())
            {
                const auto declaration = declarationIt.next();
                const QString type = declaration.captured(1);
                if(!structMembers.contains(type) && !builtinTypeRe.match(type).hasMatch())
                    continue;

                for(QString declarator : splitArgs(declaration.captured(2)))
                {
                    declarator = declarator.section('=', 0, 0).trimmed();
                    const auto nameMatch = QRegularExpression("^([A-Za-z_]\\w*)").match(declarator);
                    if(nameMatch.hasMatch())
                        addEnvironmentType(nameMatch.captured(1), type);
                }
            }

            const QString body = source.mid(bodyStart, bodyClose - bodyStart);
            if(!sensitiveFunctionMacros.isEmpty())
            {
                struct MacroExpansion
                {
                    int start = -1;
                    int length = 0;
                    QString replacement;
                };
                QVector<MacroExpansion> macroExpansions;
                int macroPos = 0;
                while(macroPos < body.size())
                {
                    if(!(body[macroPos].isLetter() || body[macroPos] == '_'))
                    {
                        ++macroPos;
                        continue;
                    }
                    const int nameStart = macroPos++;
                    while(macroPos < body.size() &&
                          (body[macroPos].isLetterOrNumber() || body[macroPos] == '_'))
                        ++macroPos;
                    const QString name = body.mid(nameStart, macroPos - nameStart);
                    const auto macroIt = sensitiveFunctionMacros.constFind(name);
                    if(macroIt == sensitiveFunctionMacros.cend()) continue;

                    int open = macroPos;
                    while(open < body.size() && body[open].isSpace()) ++open;
                    if(open >= body.size() || body[open] != '(') continue;
                    const int close = glslFindMatchingForward(body, open);
                    if(close < 0) { macroPos = open + 1; continue; }
                    const QStringList args = splitArgs(body.mid(open + 1, close - open - 1));
                    macroPos = open + 1;
                    if(args.size() != macroIt.value().params.size()) continue;

                    bool hasStructArgument = false;
                    for(const QString& arg : args)
                    {
                        const QString type = canonicalType(inferType(arg, environment));
                        if(structMembers.contains(type))
                        {
                            hasStructArgument = true;
                            break;
                        }
                    }
                    if(!hasStructArgument) continue;

                    const QString replacement = expandSensitiveMacro(macroIt.value(), args);
                    if(!replacement.isEmpty())
                        macroExpansions.push_back({nameStart, close - nameStart + 1, replacement});
                }

                if(!macroExpansions.isEmpty())
                {
                    std::sort(macroExpansions.begin(), macroExpansions.end(),
                              [](const MacroExpansion& a, const MacroExpansion& b)
                              {
                                  if(a.start != b.start) return a.start > b.start;
                                  return a.length < b.length;
                              });
                    QVector<MacroExpansion> selectedMacros;
                    for(const MacroExpansion& expansion : macroExpansions)
                    {
                        const int expansionEnd = expansion.start + expansion.length;
                        bool overlapsSelected = false;
                        for(const MacroExpansion& selected : selectedMacros)
                        {
                            const int selectedEnd = selected.start + selected.length;
                            if(expansion.start < selectedEnd && selected.start < expansionEnd)
                            {
                                overlapsSelected = true;
                                break;
                            }
                        }
                        if(!overlapsSelected) selectedMacros.push_back(expansion);
                    }
                    for(const MacroExpansion& expansion : selectedMacros)
                    {
                        edits.push_back({bodyStart + expansion.start,
                                         expansion.length,
                                         expansion.replacement});
                        ++structValuedMacroExpansionCount;
                    }
                    // Apply macro expansion first. The recursive exact-overload pass
                    // will see the exposed call tree with stable source coordinates.
                    continue;
                }
            }

            struct BodyResolvableCall
            {
                int start = -1;
                int length = 0;
                int candidateIndex = -1;
                bool unary = false;
                bool multiArgument = false;
                QStringList args;
            };
            QVector<BodyResolvableCall> resolvableCalls;
            int pos = 0;
            while(pos < body.size())
            {
                if(!(body[pos].isLetter() || body[pos] == '_'))
                {
                    ++pos;
                    continue;
                }
                const int nameStart = pos++;
                while(pos < body.size() &&
                      (body[pos].isLetterOrNumber() || body[pos] == '_'))
                    ++pos;
                const QString name = body.mid(nameStart, pos - nameStart);
                const bool hasBinaryOverloads = overloadsByName.contains(name);
                const bool hasUnaryOverloads = unaryOverloadsByName.contains(name);
                const bool hasMultiOverloads = multiOverloadsByName.contains(name);
                if(!hasBinaryOverloads && !hasUnaryOverloads && !hasMultiOverloads) continue;

                int open = pos;
                while(open < body.size() && body[open].isSpace()) ++open;
                if(open >= body.size() || body[open] != '(') continue;
                const int close = glslFindMatchingForward(body, open);
                if(close < 0) { pos = open + 1; continue; }
                const QStringList args = splitArgs(body.mid(open + 1, close - open - 1));

                // Continue inside an unresolved parent so exact nested calls are
                // still discoverable and can be normalized inside-out.
                pos = open + 1;
                const bool unaryCall = args.size() == 1;
                const bool multiArgumentCall = args.size() >= 3;
                const int candidateIndex = unaryCall
                    ? resolveExactUnaryStructOverload(name, args, environment)
                    : (multiArgumentCall
                           ? resolveExactMultiStructOverload(name, args, environment)
                           : resolveExactStructOverload(name, args, environment));
                if(candidateIndex < 0)
                {
                    traceUnresolvedCall(name, args, environment);
                    continue;
                }

                if(unaryCall)
                {
                    const SimpleUnaryFunction& candidate = unaryFunctions[candidateIndex];
                    if(candidate.name == functionMatch.captured(2) &&
                       canonicalType(candidate.paramType) == currentType1 &&
                       currentType2.isEmpty())
                        continue;
                }
                else if(multiArgumentCall)
                {
                    const SimpleMultiFunction& candidate = multiFunctions[candidateIndex];
                    bool sameSignature = candidate.name == functionMatch.captured(2) &&
                                         candidate.paramTypes.size() == currentParameterTypes.size();
                    for(int i = 0; sameSignature && i < candidate.paramTypes.size(); ++i)
                        sameSignature = canonicalType(candidate.paramTypes[i]) == currentParameterTypes[i];
                    if(sameSignature) continue;
                }
                else
                {
                    const SimpleFunction& candidate = functions[candidateIndex];
                    if(candidate.name == functionMatch.captured(2) &&
                       canonicalType(candidate.type1) == currentType1 &&
                       canonicalType(candidate.type2) == currentType2)
                        continue;
                }

                resolvableCalls.push_back({nameStart, close - nameStart + 1, candidateIndex,
                                           unaryCall, multiArgumentCall, args});
            }
            if(resolvableCalls.isEmpty()) continue;

            std::sort(resolvableCalls.begin(), resolvableCalls.end(),
                      [](const BodyResolvableCall& a, const BodyResolvableCall& b)
                      {
                          if(a.start != b.start) return a.start > b.start;
                          return a.length < b.length;
                      });

            QVector<Edit> selectedBodyEdits;
            for(const BodyResolvableCall& call : resolvableCalls)
            {
                const int callEnd = call.start + call.length;
                bool overlapsSelected = false;
                for(const Edit& selected : selectedBodyEdits)
                {
                    const int selectedEnd = selected.start + selected.length;
                    if(call.start < selectedEnd && selected.start < callEnd)
                    {
                        overlapsSelected = true;
                        break;
                    }
                }
                if(overlapsSelected)
                {
                    ++insideOutDeferredCount;
                    continue;
                }

                QMap<QString, QString> replacements;
                QString candidateExpression;
                QString candidateName;
                if(call.unary)
                {
                    const SimpleUnaryFunction& candidate = unaryFunctions[call.candidateIndex];
                    replacements.insert(candidate.param, call.args[0]);
                    candidateExpression = candidate.expression;
                    candidateName = candidate.name;
                }
                else if(call.multiArgument)
                {
                    const SimpleMultiFunction& candidate = multiFunctions[call.candidateIndex];
                    for(int i = 0; i < candidate.params.size(); ++i)
                        replacements.insert(candidate.params[i], call.args[i]);
                    candidateExpression = candidate.expression;
                    candidateName = candidate.name;
                }
                else
                {
                    const SimpleFunction& candidate = functions[call.candidateIndex];
                    replacements.insert(candidate.param1, call.args[0]);
                    replacements.insert(candidate.param2, call.args[1]);
                    candidateExpression = candidate.expression;
                    candidateName = candidate.name;
                }
                QString replacement;
                if(needsPreservedCallBoundary(candidateName, candidateExpression, call.args))
                {
                    QString helper;
                    if(call.unary)
                        helper = unaryCallBoundaryHelper(call.candidateIndex);
                    else if(call.multiArgument)
                        helper = multiCallBoundaryHelper(call.candidateIndex);
                    else
                        helper = binaryCallBoundaryHelper(call.candidateIndex);
                    replacement = QString("%1(%2)").arg(helper, call.args.join(", "));
                    ++preservedCallBoundaryCount;
                }
                else
                {
                    replacement = QString("(%1)").arg(
                        substituteExpressionParams(candidateExpression, replacements));
                    if(call.unary) ++unaryInlineCount;
                    else if(call.multiArgument) ++multiArgumentInlineCount;
                }
                selectedBodyEdits.push_back({call.start, call.length, replacement});
            }

            for(const Edit& edit : selectedBodyEdits)
            {
                edits.push_back({bodyStart + edit.start, edit.length, edit.replacement});
                if(!edit.replacement.startsWith("__bo3_exact_call_"))
                {
                    ++inlineCount;
                    ++functionBodyInlineCount;
                }
            }
        }

        // A unique, non-overloaded wrapper preserves the original function-call
        // evaluation boundary while still avoiding FXC's ambiguous overload set.
        // Insert wrappers immediately after their source definitions so ordinary
        // HLSL declaration-order rules remain satisfied.
        for(auto helper = binaryCallBoundaryHelpers.cbegin();
            helper != binaryCallBoundaryHelpers.cend(); ++helper)
        {
            const SimpleFunction& candidate = functions[helper.key()];
            const QString definition = QString(
                "\n%1 %2(%3 %4, %5 %6) { return %7; }\n")
                .arg(candidate.returnType, helper.value(),
                     candidate.type1, candidate.param1,
                     candidate.type2, candidate.param2,
                     candidate.expression);
            edits.push_back({candidate.definitionEnd, 0, definition});
        }
        for(auto helper = unaryCallBoundaryHelpers.cbegin();
            helper != unaryCallBoundaryHelpers.cend(); ++helper)
        {
            const SimpleUnaryFunction& candidate = unaryFunctions[helper.key()];
            const QString definition = QString(
                "\n%1 %2(%3 %4) { return %5; }\n")
                .arg(candidate.returnType, helper.value(),
                     candidate.paramType, candidate.param,
                     candidate.expression);
            edits.push_back({candidate.definitionEnd, 0, definition});
        }
        for(auto helper = multiCallBoundaryHelpers.cbegin();
            helper != multiCallBoundaryHelpers.cend(); ++helper)
        {
            const SimpleMultiFunction& candidate = multiFunctions[helper.key()];
            const QString definition = QString(
                "\n%1 %2(%3) { return %4; }\n")
                .arg(candidate.returnType, helper.value(),
                     candidate.paramDeclarations.join(", "),
                     candidate.expression);
            edits.push_back({candidate.definitionEnd, 0, definition});
        }

        if(parenthesizedMemberInferenceCount > 0)
            notes << QString("Inferred %1 parenthesized member-chain type(s) while resolving exact overloads (pass %2).")
                         .arg(parenthesizedMemberInferenceCount)
                         .arg(pass + 1);
        if(multiArgumentReturnInferenceCount > 0)
            notes << QString("Inferred %1 exact multi-argument function return type(s) while resolving overloads (pass %2).")
                         .arg(multiArgumentReturnInferenceCount)
                         .arg(pass + 1);
        if(transitiveSensitiveMacroCount > 0)
            notes << QString("Detected %1 transitive GLSL macro wrapper(s) around custom-struct overload calls (pass %2).")
                         .arg(transitiveSensitiveMacroCount)
                         .arg(pass + 1);
        if(preservedCallBoundaryCount > 0)
            notes << QString("Preserved function-call evaluation for %1 exact custom-struct overload call(s) with potentially side-effectful expressions (pass %2).")
                         .arg(preservedCallBoundaryCount)
                         .arg(pass + 1);
        if(edits.isEmpty()) return source;
        std::sort(edits.begin(), edits.end(),
                  [](const Edit& a, const Edit& b) { return a.start > b.start; });
        for(const Edit& edit : edits)
            source.replace(edit.start, edit.length, edit.replacement);

        if(inlineCount > 0)
            notes << QString("Inlined %1 exact custom-struct overload call(s) to avoid legacy FXC X3067 ambiguity (pass %2).")
                         .arg(inlineCount)
                         .arg(pass + 1);
        if(unaryInlineCount > 0)
            notes << QString("Inlined %1 exact unary custom-struct overload call(s) to avoid legacy FXC X3067 ambiguity (pass %2).")
                         .arg(unaryInlineCount)
                         .arg(pass + 1);
        if(multiArgumentInlineCount > 0)
            notes << QString("Inlined %1 multi-argument exact custom-struct overload call(s) to avoid legacy FXC X3067 ambiguity (pass %2).")
                         .arg(multiArgumentInlineCount)
                         .arg(pass + 1);
        if(functionBodyInlineCount > 0)
            notes << QString("Inlined %1 function-body exact custom-struct overload call(s) using conservative parameter/local type inference (pass %2).")
                         .arg(functionBodyInlineCount)
                         .arg(pass + 1);
        if(structValuedMacroExpansionCount > 0)
            notes << QString("Expanded %1 struct-valued GLSL macro invocation(s) to expose exact custom-struct overload calls (pass %2).")
                         .arg(structValuedMacroExpansionCount)
                         .arg(pass + 1);
        if(insideOutDeferredCount > 0)
            notes << QString("Deferred %1 parent exact overload call(s) for inside-out normalization (pass %2).")
                         .arg(insideOutDeferredCount)
                         .arg(pass + 1);

        // Inlining one exact overload can expose another overload call that was
        // hidden inside the callee body.  A common macro-generated AD pattern is:
        //
        //   DAm2 suab(DAm2 p, vec3 s) { return DAm2(suab(p.x,s.x), ...); }
        //   w13  suab(w13  p, vec1 s) { return su(ab(p),s); }
        //
        // The first pass replaces suab(p.x,s.x) with su(ab(p.x),s.x).  Re-run
        // the conservative exact resolver so the newly visible su(w13,float) can
        // also be selected before legacy FXC sees the overload family.  Keep a
        // hard cap to prevent pathological mutually recursive overload sets from
        // expanding forever.
        // Deep macro-generated call trees can legitimately require one pass per
        // nesting level because overlapping parents are normalized inside-out.
        // Six passes was insufficient even for compact eight-level expressions,
        // and safe macro expansion can expose finite call trees deeper than sixteen
        // levels. Retain a firm cap, but leave enough room for realistic generated
        // AD code to consume every parent that the preceding pass explicitly deferred.
        constexpr int kMaxExactStructOverloadPasses = 32;
        if(pass + 1 < kMaxExactStructOverloadPasses)
            return inlineGlslExactStructOverloadCalls(source, notes, pass + 1);

        notes << QString("Stopped exact custom-struct overload inlining after %1 passes (safety cap).")
                     .arg(kMaxExactStructOverloadPasses);
        return source;
    }

    QString normalizeGlslRelaxedScalarConstructors(QString source, QStringList& notes) const
    {
        const QRegularExpression intCtor("\\bint\\s*\\(");
        if(!intCtor.match(source).hasMatch()) return source;
        source.replace(intCtor, "GLSL_INT(");
        notes << "Routed GLSL int(...) casts through a compatibility helper; vector inputs use their first component when source GLSL relies on undefined vector-to-scalar casting.";
        return source;
    }

    QString normalizeGlslDynamicVectorWrites(QString source, QStringList& notes) const
    {
        // HLSL/FXC may hard-fail when a GLSL shader dynamically indexes a vector
        // outside its width.  For plain vector variables, lower simple component
        // writes through a modulo-wrapped helper.  Do NOT treat vector arrays
        // (`vec3 values[8]`) as vectors: `values[i]` addresses an array element,
        // not a component.
        QMap<QString,int> vectorWidths;
        QSet<QString> ambiguousNames;

        const QRegularExpression declRe(
            "\\bvec([234])\\s+([A-Za-z_]\\w*)\\s*(\\[[^\\]]*\\])?");
        auto dit = declRe.globalMatch(source);
        while(dit.hasNext())
        {
            const auto m = dit.next();
            const QString name = m.captured(2);
            if(!m.captured(3).isEmpty())
            {
                ambiguousNames.insert(name);
                vectorWidths.remove(name);
                continue;
            }

            const int width = m.captured(1).toInt();
            if(ambiguousNames.contains(name))
                continue;
            if(vectorWidths.contains(name) && vectorWidths.value(name) != width)
            {
                ambiguousNames.insert(name);
                vectorWidths.remove(name);
                continue;
            }
            vectorWidths.insert(name, width);
        }

        bool changed = false;
        for(auto it = vectorWidths.cbegin(); it != vectorWidths.cend(); ++it)
        {
            const QString name = it.key();
            const int width = it.value();
            const QString escaped = QRegularExpression::escape(name);
            const QRegularExpression writeRe(
                QString("\\b%1\\s*\\[\\s*([A-Za-z_]\\w*)\\s*\\]\\s*=\\s*([^;]+);")
                    .arg(escaped));
            int from = 0;
            while(from < source.size())
            {
                const auto m = writeRe.match(source, from);
                if(!m.hasMatch()) break;
                const QString replacement =
                    QString("GLSL_SET_VEC%1(%2, %3, %4);")
                        .arg(width)
                        .arg(name, m.captured(1), m.captured(2).trimmed());
                source.replace(m.capturedStart(), m.capturedLength(), replacement);
                from = m.capturedStart() + replacement.size();
                changed = true;
            }
        }
        if(changed)
            notes << "Lowered dynamic GLSL vector-component writes through wrapped-index helpers so out-of-range source indexing has deterministic FXC behavior.";
        return source;
    }


    QString renameGlslTypeShadowingLocals(QString source, QStringList& notes) const
    {
        // GLSL permits a local variable to have the same spelling as a struct
        // type (`ray ray;`). FXC cannot reliably parse `ray ray = (ray)0;`.
        // Rename only the variable and its expression/member uses while leaving
        // the struct type and constructor spelling untouched.
        QStringList structNames;
        const QRegularExpression structRe("\\bstruct\\s+([A-Za-z_]\\w*)\\s*\\{");
        auto sit = structRe.globalMatch(source);
        while(sit.hasNext())
            structNames << sit.next().captured(1);
        structNames.removeDuplicates();

        struct Edit
        {
            int start = -1;
            int length = 0;
            QString replacement;
        };
        QVector<Edit> edits;
        QStringList renamed;

        for(const QString& typeName : structNames)
        {
            const QRegularExpression declRe(
                QString("\\b%1\\s+(%1)\\b")
                    .arg(QRegularExpression::escape(typeName)));

            auto dit = declRe.globalMatch(source);
            int suffix = 0;
            while(dit.hasNext())
            {
                const auto dm = dit.next();
                const int varStart = static_cast<int>(dm.capturedStart(1));
                if(varStart < 0)
                    continue;

                // Require the declaration to live inside a brace-delimited
                // function/block. Struct declarations themselves do not match
                // the repeated `Type Type` shape.
                QVector<int> stack;
                for(int i = 0; i < varStart; ++i)
                {
                    if(source[i] == '{') stack.push_back(i);
                    else if(source[i] == '}' && !stack.isEmpty()) stack.pop_back();
                }
                if(stack.isEmpty())
                    continue;

                const int blockOpen = stack.back();
                const int blockClose = glslFindMatchingForward(source, blockOpen);
                if(blockClose < 0 || varStart >= blockClose)
                    continue;

                const QString replacement =
                    QString("_glsl_local_type_%1_%2").arg(typeName).arg(++suffix);
                edits.push_back({
                    varStart,
                    static_cast<int>(dm.capturedLength(1)),
                    replacement
                });

                // Rename expression/member occurrences after the declaration.
                // Type uses are recognizable because the token is followed by
                // another identifier or by '(' (a struct constructor), and are
                // intentionally left unchanged.
                const QRegularExpression wordRe(
                    QString("\\b%1\\b").arg(QRegularExpression::escape(typeName)));
                auto wit = wordRe.globalMatch(source, dm.capturedEnd(1));
                while(wit.hasNext())
                {
                    const auto wm = wit.next();
                    const int p = static_cast<int>(wm.capturedStart());
                    if(p >= blockClose)
                        break;

                    int next = static_cast<int>(wm.capturedEnd());
                    while(next < blockClose && source[next].isSpace()) ++next;
                    if(next < blockClose &&
                       (source[next] == '(' ||
                        source[next].isLetter() || source[next] == '_'))
                        continue; // constructor or another declaration/type use

                    int prev = p - 1;
                    while(prev > blockOpen && source[prev].isSpace()) --prev;
                    int prevWordEnd = prev + 1;
                    while(prev > blockOpen &&
                          (source[prev].isLetterOrNumber() || source[prev] == '_'))
                        --prev;
                    const QString prevWord =
                        source.mid(prev + 1, prevWordEnd - prev - 1);
                    if(prevWord == "struct")
                        continue;

                    edits.push_back({
                        p,
                        static_cast<int>(wm.capturedLength()),
                        replacement
                    });
                }

                renamed << typeName;
            }
        }

        if(edits.isEmpty())
            return source;

        std::sort(edits.begin(), edits.end(),
                  [](const Edit& a, const Edit& b) { return a.start > b.start; });

        int lastStart = static_cast<int>(source.size()) + 1;
        for(const Edit& edit : edits)
        {
            if(edit.start < 0 || edit.start + edit.length > source.size())
                continue;
            if(edit.start >= lastStart)
                continue;
            source.replace(edit.start, edit.length, edit.replacement);
            lastStart = edit.start;
        }

        renamed.removeDuplicates();
        notes << QString("Renamed GLSL locals that shadow user-struct type names for FXC: %1.")
                     .arg(renamed.join(", "));
        return source;
    }

    QString lowerGlslGolfedCommaTernaries(QString source, QStringList& notes) const
    {
        // Golfed Shadertoy code often uses the comma operator inside a ternary
        // as a statement:
        //
        //   cond ? a=1., b=2., nested ? c=3. : c=4. : d=5.;
        //
        // GLSL accepts this compact form. Legacy HLSL/FXC rejects commas in
        // conditional-expression branches. Lower only *statement-level*
        // ternaries into ordinary if/else blocks; assignment RHS ternaries
        // (`x = cond ? a : b`) remain untouched.
        auto braceDepthAt = [&](int pos)
        {
            int depth = 0;
            for(int i = 0; i < pos && i < source.size(); ++i)
            {
                if(source[i] == '{') ++depth;
                else if(source[i] == '}' && depth > 0) --depth;
            }
            return depth;
        };

        auto splitTopLevelSequence = [](const QString& expr)
        {
            QStringList parts;
            int paren = 0, bracket = 0, brace = 0, ternary = 0;
            int start = 0;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(') ++paren;
                else if(c == ')' && paren > 0) --paren;
                else if(c == '[') ++bracket;
                else if(c == ']' && bracket > 0) --bracket;
                else if(c == '{') ++brace;
                else if(c == '}' && brace > 0) --brace;
                else if(paren == 0 && bracket == 0 && brace == 0)
                {
                    if(c == '?') ++ternary;
                    else if(c == ':' && ternary > 0) --ternary;
                    else if(c == ',' && ternary == 0)
                    {
                        parts << expr.mid(start, i - start).trimmed();
                        start = i + 1;
                    }
                }
            }
            parts << expr.mid(start).trimmed();
            return parts;
        };

        auto hasTopLevelAssignment = [](const QString& expr)
        {
            int paren = 0, bracket = 0, brace = 0;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(') ++paren;
                else if(c == ')' && paren > 0) --paren;
                else if(c == '[') ++bracket;
                else if(c == ']' && bracket > 0) --bracket;
                else if(c == '{') ++brace;
                else if(c == '}' && brace > 0) --brace;
                else if(c == '=' && paren == 0 && bracket == 0 && brace == 0)
                {
                    const QChar prev = i > 0 ? expr[i-1] : QChar();
                    const QChar next = i + 1 < expr.size() ? expr[i+1] : QChar();
                    if(prev != '=' && prev != '!' && prev != '<' && prev != '>' &&
                       next != '=')
                        return true;
                }
            }
            return false;
        };

        std::function<QString(const QString&, bool&)> lowerSequence;
        std::function<QString(const QString&, bool&)> lowerTerm;

        lowerTerm = [&](const QString& raw, bool& changed) -> QString
        {
            const QString expr = raw.trimmed();
            if(expr.isEmpty())
                return {};

            int paren = 0, bracket = 0, brace = 0;
            int qPos = -1;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(') ++paren;
                else if(c == ')' && paren > 0) --paren;
                else if(c == '[') ++bracket;
                else if(c == ']' && bracket > 0) --bracket;
                else if(c == '{') ++brace;
                else if(c == '}' && brace > 0) --brace;
                else if(c == '?' && paren == 0 && bracket == 0 && brace == 0)
                {
                    qPos = i;
                    break;
                }
            }

            if(qPos < 0)
            {
                // In a discarded comma/ternary branch, a bare identifier or
                // literal is a deliberate no-op (common in golfed GLSL).
                if(QRegularExpression(
                        "^(?:[A-Za-z_]\\w*(?:\\.[A-Za-z_]\\w*)?|[0-9]+(?:\\.[0-9]*)?)$")
                        .match(expr).hasMatch())
                    return {};
                return expr + ";";
            }

            const QString condition = expr.left(qPos).trimmed();
            if(condition.isEmpty() || hasTopLevelAssignment(condition))
                return expr + ";";

            int nested = 0;
            paren = bracket = brace = 0;
            int colonPos = -1;
            for(int i = qPos + 1; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(') ++paren;
                else if(c == ')' && paren > 0) --paren;
                else if(c == '[') ++bracket;
                else if(c == ']' && bracket > 0) --bracket;
                else if(c == '{') ++brace;
                else if(c == '}' && brace > 0) --brace;
                else if(paren == 0 && bracket == 0 && brace == 0)
                {
                    if(c == '?') ++nested;
                    else if(c == ':')
                    {
                        if(nested == 0)
                        {
                            colonPos = i;
                            break;
                        }
                        --nested;
                    }
                }
            }

            if(colonPos < 0)
                return expr + ";";

            bool thenChanged = false;
            bool elseChanged = false;
            const QString thenCode =
                lowerSequence(expr.mid(qPos + 1, colonPos - qPos - 1), thenChanged);
            const QString elseCode =
                lowerSequence(expr.mid(colonPos + 1), elseChanged);

            changed = true;
            QString out = QString("if (%1) {\n%2\n}").arg(condition, thenCode);
            if(!elseCode.trimmed().isEmpty())
                out += QString(" else {\n%1\n}").arg(elseCode);
            return out;
        };

        lowerSequence = [&](const QString& expr, bool& changed) -> QString
        {
            const QStringList parts = splitTopLevelSequence(expr);
            QStringList statements;
            for(const QString& part : parts)
            {
                bool termChanged = false;
                const QString lowered = lowerTerm(part, termChanged);
                changed = changed || termChanged;
                if(!lowered.trimmed().isEmpty())
                    statements << lowered;
            }
            return statements.join("\n");
        };

        bool anyChanged = false;

        // First lower unbraced for-loop bodies, because otherwise the statement
        // scanner below sees the leading `for (...)` and correctly refuses to
        // treat it as a plain expression statement.
        int searchFrom = 0;
        while(searchFrom < source.size())
        {
            const auto fm = QRegularExpression("\\bfor\\s*\\(").match(source, searchFrom);
            if(!fm.hasMatch()) break;

            if(braceDepthAt(static_cast<int>(fm.capturedStart())) == 0)
            {
                searchFrom = fm.capturedEnd();
                continue;
            }

            const int open = source.indexOf('(', fm.capturedStart());
            const int close = glslFindMatchingForward(source, open);
            if(open < 0 || close < 0) break;

            int bodyStart = close + 1;
            while(bodyStart < source.size() && source[bodyStart].isSpace()) ++bodyStart;
            if(bodyStart >= source.size() || source[bodyStart] == '{')
            {
                searchFrom = close + 1;
                continue;
            }

            int paren = 0, bracket = 0, bodyEnd = -1;
            for(int i = bodyStart; i < source.size(); ++i)
            {
                const QChar c = source[i];
                if(c == '(') ++paren;
                else if(c == ')' && paren > 0) --paren;
                else if(c == '[') ++bracket;
                else if(c == ']' && bracket > 0) --bracket;
                else if(c == ';' && paren == 0 && bracket == 0)
                {
                    bodyEnd = i;
                    break;
                }
                else if(c == '{' || c == '}')
                    break;
            }
            if(bodyEnd < 0)
            {
                searchFrom = close + 1;
                continue;
            }

            const QString bodyExpr = source.mid(bodyStart, bodyEnd - bodyStart);
            if(!bodyExpr.contains('?'))
            {
                searchFrom = bodyEnd + 1;
                continue;
            }

            bool changed = false;
            const QString lowered = lowerSequence(bodyExpr, changed);
            if(!changed)
            {
                searchFrom = bodyEnd + 1;
                continue;
            }

            const QString replacement = "{\n" + lowered + "\n}";
            source.replace(bodyStart, bodyEnd - bodyStart + 1, replacement);
            searchFrom = bodyStart + replacement.size();
            anyChanged = true;
        }

        // Then lower standalone expression statements between braces/semicolons.
        const QRegularExpression stmtRe(
            "(^|[;{}])([ \\t\\r\\n]*)([^;{}]+\\?[^;{}]+);",
            QRegularExpression::MultilineOption);

        searchFrom = 0;
        while(searchFrom < source.size())
        {
            const auto sm = stmtRe.match(source, searchFrom);
            if(!sm.hasMatch()) break;

            if(braceDepthAt(static_cast<int>(sm.capturedStart(3))) == 0)
            {
                searchFrom = sm.capturedEnd();
                continue;
            }

            const QString expr = sm.captured(3).trimmed();
            const QString firstWord =
                QRegularExpression("^([A-Za-z_]\\w*)").match(expr).captured(1);
            static const QSet<QString> skipWords = {
                "if","for","while","switch","return","case","struct",
                "float","int","uint","bool",
                "vec2","vec3","vec4","ivec2","ivec3","ivec4",
                "uvec2","uvec3","uvec4","bvec2","bvec3","bvec4",
                "mat2","mat3","mat4"
            };
            if(skipWords.contains(firstWord))
            {
                searchFrom = sm.capturedEnd();
                continue;
            }

            bool changed = false;
            const QString lowered = lowerSequence(expr, changed);
            if(!changed)
            {
                searchFrom = sm.capturedEnd();
                continue;
            }

            const QString replacement =
                sm.captured(1) + sm.captured(2) + lowered;
            source.replace(sm.capturedStart(), sm.capturedLength(), replacement);
            searchFrom = sm.capturedStart() + replacement.size();
            anyChanged = true;
        }

        if(anyChanged)
            notes << "Lowered statement-level GLSL comma/ternary expressions into FXC-safe if/else statements.";
        return source;
    }

    QString stabilizeHlslLongLoopTextureSampling(QString source, QStringList& notes) const
    {
        // This fallback is only needed when the converted shader actually has
        // implicit-derivative texture sampling. Adding [loop] to unrelated long
        // loops can be actively harmful: FXC must unroll some dynamically
        // indexed vector/array l-values and rejects a forced-unroll loop that is
        // simultaneously marked [loop] (X3531).
        if(!source.contains("GLSL_TEXTURE"))
            return source;

        // FXC forces loops containing implicit-derivative Sample/SampleBias calls
        // to unroll. Long raymarch loops then fail with X3511. Detect obviously
        // long loops and use explicit LOD sampling for implicit GLSL texture()
        // calls in that shader. This is a compatibility fallback: LOD 0 (or the
        // authored bias as an approximate LOD) avoids derivative requirements.
        QMap<QString, double> numericDefines;
        const QRegularExpression defineRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s+([0-9]+(?:\\.[0-9]*)?)\\s*$",
            QRegularExpression::MultilineOption);
        auto dit = defineRe.globalMatch(source);
        while(dit.hasNext())
        {
            const auto m = dit.next();
            numericDefines[m.captured(1)] = m.captured(2).toDouble();
        }

        QVector<int> longLoopPositions;
        const QRegularExpression forRe("\\bfor\\s*\\(([^;]*);([^;]*);([^)]*)\\)");
        auto fit = forRe.globalMatch(source);
        while(fit.hasNext())
        {
            const auto m = fit.next();
            const QString cond = m.captured(2);
            const auto bm = QRegularExpression(
                "(?:<|<=)\\s*([A-Za-z_]\\w*|[0-9]+(?:\\.[0-9]*)?)")
                .match(cond);
            if(!bm.hasMatch())
                continue;

            const QString boundToken = bm.captured(1);
            bool numeric = false;
            double bound = boundToken.toDouble(&numeric);
            if(!numeric && numericDefines.contains(boundToken))
            {
                bound = numericDefines.value(boundToken);
                numeric = true;
            }

            if(numeric && bound >= 64.0)
                longLoopPositions << static_cast<int>(m.capturedStart());
        }

        if(longLoopPositions.isEmpty())
            return source;

        // Encourage FXC to keep those loops dynamic.
        std::sort(longLoopPositions.begin(), longLoopPositions.end(), std::greater<int>());
        for(const int pos : longLoopPositions)
        {
            const int probeStart = qMax(0, pos - 16);
            const QString probe = source.mid(probeStart, pos - probeStart);
            if(!probe.contains(QRegularExpression("\\[\\s*loop\\s*\\]\\s*$")))
                source.insert(pos, "[loop] ");
        }

        auto rewriteCalls = [&](const QString& functionName,
                                const std::function<QString(const QStringList&)>& build)
        {
            int from = 0;
            while(from < source.size())
            {
                int pos = source.indexOf(functionName, from, Qt::CaseSensitive);
                if(pos < 0) break;
                const bool leftOk =
                    pos == 0 || !(source[pos-1].isLetterOrNumber() || source[pos-1] == '_');
                int after = pos + functionName.size();
                const bool rightOk =
                    after >= source.size() ||
                    !(source[after].isLetterOrNumber() || source[after] == '_');
                if(!leftOk || !rightOk) { from = after; continue; }
                while(after < source.size() && source[after].isSpace()) ++after;
                if(after >= source.size() || source[after] != '(') { from = after; continue; }

                const int close = glslFindMatchingForward(source, after);
                if(close < 0) break;
                const QString inside = source.mid(after + 1, close - after - 1);

                QStringList args;
                int depth = 0, start = 0;
                for(int i = 0; i < inside.size(); ++i)
                {
                    const QChar c = inside[i];
                    if(c == '(' || c == '[' || c == '{') ++depth;
                    else if(c == ')' || c == ']' || c == '}') --depth;
                    else if(c == ',' && depth == 0)
                    {
                        args << inside.mid(start, i - start).trimmed();
                        start = i + 1;
                    }
                }
                args << inside.mid(start).trimmed();

                const QString replacement = build(args);
                if(replacement.isEmpty()) { from = close + 1; continue; }
                source.replace(pos, close - pos + 1, replacement);
                from = pos + replacement.size();
            }
        };

        rewriteCalls("GLSL_TEXTURE_S", [](const QStringList& a) -> QString {
            if(a.size() == 3)
                return QString("GLSL_TEXTURE_LEVEL_S(%1, %2, %3, 0.0)")
                    .arg(a[0], a[1], a[2]);
            return {};
        });
        rewriteCalls("GLSL_TEXTURE_BIAS_S", [](const QStringList& a) -> QString {
            if(a.size() == 4)
                return QString("GLSL_TEXTURE_LEVEL_S(%1, %2, %3, %4)")
                    .arg(a[0], a[1], a[2], a[3]);
            return {};
        });
        rewriteCalls("GLSL_TEXTURE", [](const QStringList& a) -> QString {
            if(a.size() == 2)
                return QString("GLSL_TEXTURE_LEVEL(%1, %2, 0.0)").arg(a[0], a[1]);
            return {};
        });
        rewriteCalls("GLSL_TEXTURE_BIAS", [](const QStringList& a) -> QString {
            if(a.size() == 3)
                return QString("GLSL_TEXTURE_LEVEL(%1, %2, %3)").arg(a[0], a[1], a[2]);
            return {};
        });

        notes << "Used explicit-LOD texture sampling and [loop] on long GLSL loops to avoid FXC derivative-driven unroll failures.";
        return source;
    }


QString renameGlslHlslReservedIdentifiers(QString source, QStringList& notes) const
    {
        // GLSL permits several identifiers that legacy FXC reserves for HLSL
        // effects/geometry syntax. They are ordinary user symbols in Shadertoy,
        // but FXC rejects declarations such as:
        //     float line(...), vec3 point, vec3 vector, float pass
        //
        // These names do not name GLSL builtins, so whole-word renaming is safe.
        const QStringList directNames = {
            "line", "point", "vector", "pass", "sampler"
        };

        QStringList renamed;
        for(const QString& name : directNames)
        {
            if(!source.contains(QRegularExpression(
                    QString("\\b%1\\b").arg(QRegularExpression::escape(name)))))
                continue;

            const QString replacement = QString("_glsl_hlsl_%1").arg(name);
            source.replace(
                QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(name))),
                replacement);
            renamed << name;
        }

        // `texture` is different: it is also the GLSL texture() builtin.
        // Rename variable uses but leave function calls untouched.
        const QRegularExpression textureDecl(
            "\\b(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?)|[A-Za-z_]\\w*)"
            "\\b[^;{}\\n]*\\btexture\\b(?!\\s*\\()");
        if(textureDecl.match(source).hasMatch())
        {
            // Keep GLSL texture(...) builtin calls intact, but rename declarations
            // and variable/member uses. The wider declaration probe also catches
            // comma lists such as `vec3 base, extremity, texture;`.
            source.replace(QRegularExpression("\\btexture\\b(?!\\s*\\()"), "_glsl_hlsl_texture");
            renamed << "texture";
        }

        if(!renamed.isEmpty())
            notes << QString("Renamed GLSL identifiers reserved by legacy HLSL/FXC: %1.")
                         .arg(renamed.join(", "));

        return source;
    }

    QString renameGlslBo3HeaderCollisions(QString source, QStringList& notes) const
    {
        // BO3's shared shader headers inject these names before user code.
        // A legal GLSL user function/constant with the same spelling becomes an
        // HLSL redefinition (or, for M_PI, may be macro-expanded into a number).
        const QStringList names = {
            "viewMatrix", "numLights", "gameTime", "renderTargetSize",
            "hdrControl0", "hdrControl1", "relHDRExposure", "M_PI"
        };

        QStringList renamed;
        for(const QString& name : names)
        {
            if(!source.contains(QRegularExpression(
                    QString("\\b%1\\b").arg(QRegularExpression::escape(name)))))
                continue;

            const QString replacement = QString("_glsl_bo3_%1").arg(name);
            source.replace(
                QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(name))),
                replacement);
            renamed << name;
        }

        if(!renamed.isEmpty())
            notes << QString("Renamed GLSL symbols that collide with BO3 shared-header globals/macros: %1.")
                         .arg(renamed.join(", "));

        return source;
    }

    QString renameGlslFunctionShadowingLocals(QString source, QStringList& notes) const
    {
        // GLSL allows a local being declared to have the same spelling as a
        // function called in its initializer:
        //     float arg = arg(z);
        // FXC makes the local visible too early and resolves arg(z) as a variable.
        // Rename the local from its declaration onward, while leaving the
        // initializer bound to the original function.
        QSet<QString> functionNames;
        const QRegularExpression functionRe(
            "\\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?)|[A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\(");
        auto fit = functionRe.globalMatch(source);
        while(fit.hasNext())
            functionNames.insert(fit.next().captured(1));

        if(functionNames.isEmpty())
            return source;

        struct Decl
        {
            int nameStart = -1;
            int nameLength = 0;
            QString name;
        };
        QVector<Decl> decls;

        for(const QString& name : functionNames)
        {
            const QRegularExpression declRe(
                QString("\\b(?:const\\s+)?(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?)|[A-Za-z_]\\w*)\\s+(%1)\\s*(?==)")
                    .arg(QRegularExpression::escape(name)));
            auto it = declRe.globalMatch(source);
            while(it.hasNext())
            {
                const auto m = it.next();
                decls.push_back({
                    static_cast<int>(m.capturedStart(1)),
                    static_cast<int>(m.capturedLength(1)),
                    name
                });
            }
        }

        std::sort(decls.begin(), decls.end(), [](const Decl& a, const Decl& b)
        {
            return a.nameStart > b.nameStart;
        });

        int suffix = 0;
        QStringList renamed;
        for(const Decl& d : decls)
        {
            if(d.nameStart < 0 || d.nameStart >= source.size())
                continue;

            QVector<int> stack;
            for(int i = 0; i < d.nameStart; ++i)
            {
                if(source[i] == '{') stack.push_back(i);
                else if(source[i] == '}' && !stack.isEmpty()) stack.pop_back();
            }
            if(stack.isEmpty())
                continue;

            const int blockOpen = stack.back();
            int blockClose = -1;
            int depth = 0;
            for(int i = blockOpen; i < source.size(); ++i)
            {
                if(source[i] == '{') ++depth;
                else if(source[i] == '}')
                {
                    --depth;
                    if(depth == 0) { blockClose = i; break; }
                }
            }
            if(blockClose < 0)
                continue;

            int parenDepth = 0;
            int bracketDepth = 0;
            int declEnd = -1;
            for(int i = d.nameStart + d.nameLength; i < blockClose; ++i)
            {
                const QChar c = source[i];
                if(c == '(') ++parenDepth;
                else if(c == ')') --parenDepth;
                else if(c == '[') ++bracketDepth;
                else if(c == ']') --bracketDepth;
                else if(c == ';' && parenDepth == 0 && bracketDepth == 0)
                {
                    declEnd = i;
                    break;
                }
            }
            if(declEnd < 0)
                continue;

            // Only rename the local when its own initializer actually calls the
            // same-named function (for example `float arg = arg(z);`).  A local
            // that merely shares a name with some function elsewhere is valid
            // GLSL/HLSL and may also be referenced by macros.  Renaming every
            // such local breaks those macro references (case 63's local `pf`).
            const int initializerStart = d.nameStart + d.nameLength;
            const QString initializer = source.mid(initializerStart, declEnd - initializerStart);
            const QRegularExpression selfCallRe(
                QString("(?<!\\.)\\b%1\\s*\\(")
                    .arg(QRegularExpression::escape(d.name)));
            if(!selfCallRe.match(initializer).hasMatch())
                continue;

            const QString local = QString("_glsl_local_fn_%1_%2").arg(d.name).arg(++suffix);
            source.replace(d.nameStart, d.nameLength, local);
            const int delta = local.size() - d.nameLength;
            declEnd += delta;
            blockClose += delta;

            const int scopeStart = declEnd + 1;
            if(scopeStart < blockClose)
            {
                QString scoped = source.mid(scopeStart, blockClose - scopeStart);
                scoped = replaceScopedIdentifierUsesPreservingMembers(scoped, d.name, local);
                source.replace(scopeStart, blockClose - scopeStart, scoped);
            }

            renamed << d.name;
        }

        if(!renamed.isEmpty())
        {
            renamed.removeDuplicates();
            notes << QString("Renamed GLSL locals that shadow same-named functions in their initializers: %1.")
                         .arg(renamed.join(", "));
        }
        return source;
    }

    QString normalizeGlslPrefixArrayDeclarators(QString source, QStringList& notes) const
    {
        // Some Shadertoy sources use GLSL array spelling `vec3[N] points`.
        // FXC requires HLSL spelling `float3 points[N]`. Normalize the declarator
        // while GLSL type names are still present. Constructor calls `vec3[N](...)`
        // are intentionally left alone for convertGlslArrayConstructors().
        const QString typePattern =
            "(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat(?:[234](?:x[234])?))";
        const QRegularExpression re(
            QString("\\b(%1)\\s*\\[\\s*([^\\]\\n]+)\\s*\\]\\s+([A-Za-z_]\\w*)\\b")
                .arg(typePattern));

        bool changed = false;
        int from = 0;
        while(from < source.size())
        {
            const auto m = re.match(source, from);
            if(!m.hasMatch()) break;
            const QString replacement =
                QString("%1 %3[%2]").arg(m.captured(1), m.captured(2).trimmed(), m.captured(3));
            source.replace(m.capturedStart(), m.capturedLength(), replacement);
            from = m.capturedStart() + replacement.size();
            changed = true;
        }

        if(changed)
            notes << "Normalized GLSL type[N] array declarations/parameters to FXC-compatible HLSL declarator order.";
        return source;
    }

    QString convertGlslStructConstructors(QString source, QStringList& notes) const
    {
        // GLSL structs have constructor syntax `Foo(a,b,c)`. Legacy FXC only
        // provides constructor syntax for numeric base types, so generate a small
        // factory function per simple struct and route constructor calls through it.
        //
        // Put each factory immediately after its own struct declaration. Real
        // Shadertoy code often calls one constructor before later unrelated structs
        // are declared, so placing all factories after the final struct is too late.
        struct Member { QString type; QString name; };
        struct StructInfo
        {
            QString name;
            QVector<Member> members;
        };

        QVector<StructInfo> structs;
        const QRegularExpression structRe(
            "struct\\s+([A-Za-z_]\\w*)\\s*\\{([^{}]*)\\}\\s*;",
            QRegularExpression::DotMatchesEverythingOption);
        auto it = structRe.globalMatch(source);
        while(it.hasNext())
        {
            const auto m = it.next();
            StructInfo info;
            info.name = m.captured(1);

            const QStringList statements = m.captured(2).split(';', Qt::SkipEmptyParts);
            bool supported = true;
            for(QString statement : statements)
            {
                statement = statement.trimmed();
                if(statement.isEmpty()) continue;

                const auto fm = QRegularExpression(
                    "^(?:const\\s+)?([A-Za-z_]\\w*)\\s+(.+)$",
                    QRegularExpression::DotMatchesEverythingOption).match(statement);
                if(!fm.hasMatch()) { supported = false; break; }

                const QString type = fm.captured(1);
                const QStringList names = fm.captured(2).split(',', Qt::SkipEmptyParts);
                for(QString field : names)
                {
                    field = field.trimmed();
                    if(field.contains('[') || field.contains('='))
                    {
                        supported = false;
                        break;
                    }
                    const auto nm = QRegularExpression("^([A-Za-z_]\\w*)$").match(field);
                    if(!nm.hasMatch())
                    {
                        supported = false;
                        break;
                    }
                    info.members.push_back({type, nm.captured(1)});
                }
                if(!supported) break;
            }

            if(supported && !info.members.isEmpty())
                structs.push_back(info);
        }

        if(structs.isEmpty())
            return source;

        QMap<QString, QString> factoryByStruct;
        QStringList converted;
        for(const StructInfo& info : structs)
        {
            const QString helper = QString("GLSL_STRUCT_CTOR_%1").arg(info.name);
            const QRegularExpression ctorRe(
                QString("\\b%1\\s*(?=\\()").arg(QRegularExpression::escape(info.name)));
            if(!ctorRe.match(source).hasMatch())
                continue;

            source.replace(ctorRe, helper);

            QStringList params;
            QStringList assigns;
            for(int i = 0; i < info.members.size(); ++i)
            {
                const Member& member = info.members[i];
                const QString arg = QString("_glsl_ctor_%1").arg(i);
                params << QString("%1 %2").arg(member.type, arg);
                assigns << QString("_glsl_result.%1 = %2;").arg(member.name, arg);
            }

            factoryByStruct.insert(
                info.name,
                QString("\n%1 %2(%3) { %1 _glsl_result; %4 return _glsl_result; }\n")
                    .arg(info.name, helper, params.join(", "), assigns.join(" ")));
            converted << info.name;
        }

        if(factoryByStruct.isEmpty())
            return source;

        struct FactoryInsertion { int pos = -1; QString text; };
        QVector<FactoryInsertion> insertions;
        auto sit = structRe.globalMatch(source);
        while(sit.hasNext())
        {
            const auto m = sit.next();
            const QString name = m.captured(1);
            const auto fit = factoryByStruct.constFind(name);
            if(fit != factoryByStruct.constEnd())
                insertions.push_back({static_cast<int>(m.capturedEnd()), fit.value()});
        }

        std::sort(insertions.begin(), insertions.end(),
                  [](const FactoryInsertion& a, const FactoryInsertion& b) {
                      return a.pos > b.pos;
                  });
        for(const FactoryInsertion& insertion : insertions)
            source.insert(insertion.pos, insertion.text);

        notes << QString("Converted GLSL user-struct constructors to FXC-safe factory functions: %1.")
                     .arg(converted.join(", "));
        return source;
    }

QString convertGlslArrayConstructors(QString source, QStringList& notes) const
    {
        // GLSL supports array constructors:
        //   vec3 points[3] = vec3[3](a,b,c);
        //   vec3[3] points  = vec3[3](a,b,c);   // normalized earlier
        //
        // FXC does not accept type[N](...). At global scope use an HLSL brace
        // initializer. Inside functions expand into a declaration plus element
        // assignments so symbolic sizes (N, kCount, etc.) remain valid.
        // The element type may be a built-in numeric type or a user struct.
        // The declaration and constructor must spell the same type.
        const QString typePattern = "(?:[A-Za-z_]\\w*)";
        const QRegularExpression re(
            QString("\\b(%1)\\s+([A-Za-z_]\\w*)\\s*\\[\\s*([^\\]]*)\\s*\\]\\s*=\\s*(%1)\\s*\\[\\s*([^\\]]*)\\s*\\]\\s*\\(")
                .arg(typePattern));

        auto splitArgs = [](const QString& inside)
        {
            QStringList args;
            int depth = 0;
            int start = 0;
            for(int i = 0; i < inside.size(); ++i)
            {
                const QChar c = inside[i];
                if(c == '(' || c == '[' || c == '{') ++depth;
                else if(c == ')' || c == ']' || c == '}') --depth;
                else if(c == ',' && depth == 0)
                {
                    args << inside.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            const QString tail = inside.mid(start).trimmed();
            if(!tail.isEmpty()) args << tail;
            return args;
        };

        auto braceDepthAt = [&](int pos)
        {
            int depth = 0;
            bool inString = false;
            QChar quote;
            bool escaped = false;
            for(int i = 0; i < pos && i < source.size(); ++i)
            {
                const QChar c = source[i];
                if(inString)
                {
                    if(escaped) { escaped = false; continue; }
                    if(c == '\\') { escaped = true; continue; }
                    if(c == quote) inString = false;
                    continue;
                }
                if(c == '"' || c == '\'')
                {
                    inString = true;
                    quote = c;
                    continue;
                }
                if(c == '{') ++depth;
                else if(c == '}' && depth > 0) --depth;
            }
            return depth;
        };

        bool changed = false;
        int searchFrom = 0;
        while(searchFrom < source.size())
        {
            const auto m = re.match(source, searchFrom);
            if(!m.hasMatch()) break;

            const QString declaredType = m.captured(1);
            const QString name = m.captured(2);
            QString declaredSize = m.captured(3).trimmed();
            const QString ctorType = m.captured(4);
            const QString ctorSize = m.captured(5).trimmed();

            if(declaredType != ctorType)
            {
                searchFrom = m.capturedEnd();
                continue;
            }

            const int openPos = m.capturedEnd() - 1;
            const int closePos = glslFindMatchingForward(source, openPos);
            if(closePos < 0)
                break;

            int semi = closePos + 1;
            while(semi < source.size() && source[semi].isSpace()) ++semi;
            if(semi >= source.size() || source[semi] != ';')
            {
                searchFrom = closePos + 1;
                continue;
            }

            const QString inside = source.mid(openPos + 1, closePos - openPos - 1);
            const QStringList args = splitArgs(inside);
            if(args.isEmpty())
            {
                searchFrom = semi + 1;
                continue;
            }

            if(declaredSize.isEmpty())
                declaredSize = QString::number(args.size());

            bool ctorNumeric = false;
            const int ctorCount = ctorSize.toInt(&ctorNumeric);
            if(ctorNumeric && ctorCount > 0 && ctorCount != args.size())
            {
                searchFrom = semi + 1;
                continue;
            }

            QString replacement;
            if(braceDepthAt(m.capturedStart()) == 0)
            {
                // Global/static arrays must be initialized entirely from literal
                // expressions. A user-struct constructor is lowered later to a
                // helper function call, and FXC rejects helper calls in a global
                // initializer (X3011). Convert complete struct-constructor
                // expressions to aggregate braces here instead, before the generic
                // struct-constructor pass runs. Nested user structs are handled
                // recursively; numeric/vector constructors stay as expressions.
                QSet<QString> structNames;
                const QRegularExpression structNameRe("\\bstruct\\s+([A-Za-z_]\\w*)\\s*\\{");
                auto structIt = structNameRe.globalMatch(source);
                while(structIt.hasNext())
                    structNames.insert(structIt.next().captured(1));

                // A global initializer must remain a true FXC literal all the
                // way through later converter passes.  It is not enough to turn
                // only the outer GLSL struct constructor into braces: leaving a
                // vecN(...) inside the aggregate causes the one-argument vector
                // pass to turn it into GLSL_VECN_*(), which is a function call and
                // therefore triggers X3011 at global scope.
                auto vectorWidth = [](const QString& type) -> int
                {
                    const auto vm = QRegularExpression("^(?:[iub]?vec)([234])$").match(type);
                    return vm.hasMatch() ? vm.captured(1).toInt() : 0;
                };

                std::function<bool(const QString&, QStringList&)> flattenVectorCtor;
                flattenVectorCtor = [&](const QString& expression, QStringList& components) -> bool
                {
                    const QString expr = expression.trimmed();
                    const auto head = QRegularExpression("^([A-Za-z_]\\w*)\\s*\\(").match(expr);
                    if(!head.hasMatch())
                        return false;

                    const QString type = head.captured(1);
                    const int width = vectorWidth(type);
                    if(width == 0)
                        return false;

                    const int open = expr.indexOf('(', head.capturedStart(1) + head.capturedLength(1));
                    if(open < 0)
                        return false;
                    const int close = glslFindMatchingForward(expr, open);
                    if(close < 0 || !expr.mid(close + 1).trimmed().isEmpty())
                        return false;

                    const QStringList ctorArgs = splitArgs(expr.mid(open + 1, close - open - 1));
                    if(ctorArgs.isEmpty())
                        return false;

                    QStringList flat;
                    for(const QString& ctorArg : ctorArgs)
                    {
                        QStringList nested;
                        if(flattenVectorCtor(ctorArg, nested))
                            flat << nested;
                        else
                            flat << ctorArg.trimmed();
                    }

                    // GLSL vecN(scalar) splats the scalar to every component.
                    if(flat.size() == 1 && width > 1)
                    {
                        const QString scalar = flat.front();
                        while(flat.size() < width)
                            flat << scalar;
                    }

                    if(flat.size() != width)
                        return false;
                    components = flat;
                    return true;
                };

                std::function<QString(const QString&)> aggregateGlobalLiteral;
                aggregateGlobalLiteral = [&](const QString& expression) -> QString
                {
                    const QString expr = expression.trimmed();

                    QStringList vectorComponents;
                    if(flattenVectorCtor(expr, vectorComponents))
                        return QString("{ %1 }").arg(vectorComponents.join(", "));

                    const auto head = QRegularExpression("^([A-Za-z_]\\w*)\\s*\\(").match(expr);
                    if(!head.hasMatch() || !structNames.contains(head.captured(1)))
                        return expr;

                    const int open = expr.indexOf('(', head.capturedStart(1) + head.capturedLength(1));
                    if(open < 0)
                        return expr;
                    const int close = glslFindMatchingForward(expr, open);
                    if(close < 0 || !expr.mid(close + 1).trimmed().isEmpty())
                        return expr;

                    const QStringList fields = splitArgs(expr.mid(open + 1, close - open - 1));
                    QStringList convertedFields;
                    convertedFields.reserve(fields.size());
                    for(const QString& field : fields)
                        convertedFields << aggregateGlobalLiteral(field);
                    return QString("{ %1 }").arg(convertedFields.join(", "));
                };

                QStringList globalArgs;
                globalArgs.reserve(args.size());
                for(const QString& arg : args)
                    globalArgs << aggregateGlobalLiteral(arg);

                replacement =
                    QString("%1 %2[%3] = { %4 };")
                        .arg(declaredType, name, declaredSize, globalArgs.join(", "));
            }
            else
            {
                replacement =
                    QString("%1 %2[%3];").arg(declaredType, name, declaredSize);
                for(int i = 0; i < args.size(); ++i)
                    replacement += QString(" %1[%2] = %3;").arg(name).arg(i).arg(args[i]);
            }

            source.replace(m.capturedStart(), semi - m.capturedStart() + 1, replacement);
            searchFrom = m.capturedStart() + replacement.size();
            changed = true;
        }

        if(changed)
            notes << "Expanded GLSL array constructors (including symbolic sizes and global const arrays) into BO3/FXC-safe HLSL initialization.";

        return source;
    }

    QString convertGlslSingleArgumentMatrixConstructors(QString source, QStringList& notes) const
    {
        struct ConstructorInfo { const char* glslName; const char* helperName; };
        const ConstructorInfo constructors[] = {
            {"mat2", "GLSL_MAT2"}, {"mat3", "GLSL_MAT3"}, {"mat4", "GLSL_MAT4"}
        };

        bool changed = false;
        for(const ConstructorInfo& info : constructors)
        {
            const QString functionName = QString::fromLatin1(info.glslName);
            const QString helperName = QString::fromLatin1(info.helperName);
            int searchFrom = 0;
            while(searchFrom < source.size())
            {
                const int pos = source.indexOf(functionName, searchFrom, Qt::CaseSensitive);
                if(pos < 0) break;
                const bool leftOk = pos == 0 || !(source[pos-1].isLetterOrNumber() || source[pos-1] == '_');
                int afterName = pos + functionName.size();
                const bool rightOk = afterName >= source.size() || !(source[afterName].isLetterOrNumber() || source[afterName] == '_');
                if(!leftOk || !rightOk) { searchFrom = afterName; continue; }
                while(afterName < source.size() && source[afterName].isSpace()) ++afterName;
                if(afterName >= source.size() || source[afterName] != '(') { searchFrom = afterName; continue; }

                int depth = 0;
                int close = -1;
                int topLevelCommas = 0;
                for(int i = afterName; i < source.size(); ++i)
                {
                    const QChar c = source[i];
                    if(c == '(' || c == '[' || c == '{') ++depth;
                    else if(c == ')' || c == ']' || c == '}')
                    {
                        --depth;
                        if(depth == 0) { close = i; break; }
                    }
                    else if(c == ',' && depth == 1)
                    {
                        ++topLevelCommas;
                    }
                }
                if(close < 0) break;

                // GLSL matN(scalar) creates a diagonal matrix and matN(otherMat)
                // performs identity-extended/truncated matrix conversion. FXC does
                // not preserve those constructor semantics, so use overloaded
                // helpers for all one-argument square matrix constructors.
                if(topLevelCommas == 0)
                {
                    const QString inside = source.mid(afterName + 1, close - afterName - 1).trimmed();
                    if(!inside.isEmpty())
                    {
                        const QString replacement = QString("%1(%2)").arg(helperName, inside);
                        source.replace(pos, close - pos + 1, replacement);
                        // Revisit the replacement so nested same-type matrix
                        // constructors are converted too.
                        searchFrom = pos;
                        changed = true;
                        continue;
                    }
                }
                searchFrom = close + 1;
            }
        }

        if(changed)
            notes << "Converted GLSL one-argument mat2/mat3/mat4 constructors through BO3-safe diagonal/matrix-conversion helpers.";
        return source;
    }

    QString convertGlslSingleArgumentVectorConstructors(QString source, QStringList& notes) const
    {
        struct ConstructorInfo
        {
            const char* glslName;
            const char* helperStem;
            const char* hlslScalar;
            const char* hlslVectorStem;
            int width;
        };
        const ConstructorInfo constructors[] = {
            {"vec2",  "GLSL_VEC2",  "float", "float", 2},
            {"vec3",  "GLSL_VEC3",  "float", "float", 3},
            {"vec4",  "GLSL_VEC4",  "float", "float", 4},
            {"ivec2", "GLSL_IVEC2", "int",   "int",   2},
            {"ivec3", "GLSL_IVEC3", "int",   "int",   3},
            {"ivec4", "GLSL_IVEC4", "int",   "int",   4},
            {"uvec2", "GLSL_UVEC2", "uint",  "uint",  2},
            {"uvec3", "GLSL_UVEC3", "uint",  "uint",  3},
            {"uvec4", "GLSL_UVEC4", "uint",  "uint",  4},
            {"bvec2", "GLSL_BVEC2", "bool",  "bool",  2},
            {"bvec3", "GLSL_BVEC3", "bool",  "bool",  3},
            {"bvec4", "GLSL_BVEC4", "bool",  "bool",  4}
        };

        // BO3 uses the legacy FXC overload resolver. It can consider scalar-to-vector
        // promotions while resolving an overloaded function, which makes calls such as
        // GLSL_VEC3(0) ambiguous even when exact int/float overloads exist.  Infer the
        // source expression width here and emit a uniquely named helper instead.
        // GLSL shaders frequently reuse short names such as p/q/v in many functions.
        // Resolve an identifier against the nearest declaration before the exact
        // constructor call. This is intentionally evaluated against the current
        // source text because earlier constructor rewrites can change string offsets.
        auto symbolWidthAt = [&](const QString& name, int contextPos)
        {
            const int limit = qBound(0, contextPos, source.size());
            const QString prefix = source.left(limit);
            const QString escapedName = QRegularExpression::escape(name);
            // Track scalar declarations as well as vectors. Shader-golf code often
            // defines `#define vec1 float` and then reuses a short identifier (a/p/v)
            // that was a vector in an earlier function. If scalar declarations are
            // ignored, the older vector declaration wins and vecN(a) is lowered with
            // the wrong vector helper/cast, producing FXC X3014.
            QRegularExpression declRe(
                QString("\\b(float|int|uint|bool|vec1|vec[234]|ivec[234]|uvec[234]|bvec[234])\\s+%1\\b")
                    .arg(escapedName));
            auto it = declRe.globalMatch(prefix);
            int width = 1;
            while(it.hasNext())
            {
                const QString type = it.next().captured(1);
                const auto wm = QRegularExpression("([234])$").match(type);
                width = wm.hasMatch() ? wm.captured(1).toInt() : 1;
            }
            return width;
        };

        QMap<QString, int> functionWidths;
        QRegularExpression functionDeclRe("\\b(float|int|uint|bool|vec1|vec[234]|ivec[234]|uvec[234]|bvec[234])\\s+([A-Za-z_]\\w*)\\s*\\(");
        auto fnIt = functionDeclRe.globalMatch(source);
        while(fnIt.hasNext())
        {
            const auto m = fnIt.next();
            const QString type = m.captured(1);
            int width = 1;
            QRegularExpression widthRe("([234])$");
            const auto wm = widthRe.match(type);
            if(wm.hasMatch()) width = wm.captured(1).toInt();
            functionWidths[m.captured(2)] = width;
        }

        auto stripOuterParens = [](QString expr)
        {
            expr = expr.trimmed();
            bool changed = true;
            while(changed && expr.size() >= 2 && expr.front() == '(' && expr.back() == ')')
            {
                changed = false;
                int depth = 0;
                bool wrapsWhole = true;
                for(int i = 0; i < expr.size(); ++i)
                {
                    const QChar c = expr[i];
                    if(c == '(') ++depth;
                    else if(c == ')')
                    {
                        --depth;
                        if(depth == 0 && i != expr.size() - 1)
                        {
                            wrapsWhole = false;
                            break;
                        }
                    }
                }
                if(wrapsWhole && depth == 0)
                {
                    expr = expr.mid(1, expr.size() - 2).trimmed();
                    changed = true;
                }
            }
            return expr;
        };

        auto splitTopLevelArgs = [](const QString& args)
        {
            QStringList result;
            int depth = 0;
            int start = 0;
            for(int i = 0; i < args.size(); ++i)
            {
                const QChar c = args[i];
                if(c == '(' || c == '[' || c == '{') ++depth;
                else if(c == ')' || c == ']' || c == '}') --depth;
                else if(c == ',' && depth == 0)
                {
                    result << args.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            result << args.mid(start).trimmed();
            return result;
        };

        std::function<int(QString,int)> inferWidth;
        inferWidth = [&](QString expr, int contextPos) -> int
        {
            expr = stripOuterParens(expr);
            if(expr.isEmpty()) return 1;

            // Numeric / boolean literals and explicit scalar casts.
            if(QRegularExpression("^[+-]?(?:(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?[fFuU]?|0[xX][0-9A-Fa-f]+[uU]?)$").match(expr).hasMatch())
                return 1;
            if(expr == "true" || expr == "false") return 1;
            if(QRegularExpression("^(?:float|int|uint|bool)\\s*\\(").match(expr).hasMatch()) return 1;

            if(QRegularExpression("^[A-Za-z_]\\w*$").match(expr).hasMatch())
                return symbolWidthAt(expr, contextPos);

            // A terminal swizzle fixes the expression width directly.
            QRegularExpression swizzleEndRe("\\.\\s*([xyzwrgba]{1,4})\\s*$");
            const auto swz = swizzleEndRe.match(expr);
            if(swz.hasMatch()) return swz.captured(1).size();

            // Ternaries return the widest of their value branches.
            int depth = 0;
            int qPos = -1;
            int colonPos = -1;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(' || c == '[' || c == '{') ++depth;
                else if(c == ')' || c == ']' || c == '}') --depth;
                else if(depth == 0 && c == '?' && qPos < 0) qPos = i;
                else if(depth == 0 && c == ':' && qPos >= 0) { colonPos = i; break; }
            }
            if(qPos >= 0 && colonPos > qPos)
                return qMax(inferWidth(expr.mid(qPos + 1, colonPos - qPos - 1), contextPos), inferWidth(expr.mid(colonPos + 1), contextPos));

            // Comparisons/logical expressions are scalar booleans in GLSL when they
            // reach a one-argument constructor here.
            depth = 0;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(' || c == '[' || c == '{') ++depth;
                else if(c == ')' || c == ']' || c == '}') --depth;
                else if(depth == 0)
                {
                    const QString two = expr.mid(i, 2);
                    if(two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||") return 1;
                    if(c == '<' || c == '>') return 1;
                }
            }

            // Whole constructor/function call.
            QRegularExpression callRe("^([A-Za-z_]\\w*)\\s*\\(");
            const auto cm = callRe.match(expr);
            if(cm.hasMatch())
            {
                const QString name = cm.captured(1);
                const int open = expr.indexOf('(', cm.capturedEnd(1));
                int d = 0;
                int close = -1;
                for(int i = open; i < expr.size(); ++i)
                {
                    const QChar c = expr[i];
                    if(c == '(' || c == '[' || c == '{') ++d;
                    else if(c == ')' || c == ']' || c == '}')
                    {
                        --d;
                        if(d == 0) { close = i; break; }
                    }
                }
                if(close == expr.size() - 1)
                {
                    QRegularExpression vecCtorRe("^(?:vec|ivec|uvec|bvec)([234])$");
                    const auto vm = vecCtorRe.match(name);
                    if(vm.hasMatch()) return vm.captured(1).toInt();
                    QRegularExpression matCtorRe("^mat([234])(?:x([234]))?$");
                    const auto mm = matCtorRe.match(name);
                    if(mm.hasMatch()) return mm.captured(1).toInt();

                    // A nested one-argument constructor may already have been rewritten
                    // during this pass (for example uvec3(ivec3(p))). Its deterministic
                    // helper name carries the resulting vector width explicitly.
                    QRegularExpression helperVecRe("^GLSL_(?:I|U|B)?VEC([234])_(?:S|V[234])$");
                    const auto hm = helperVecRe.match(name);
                    if(hm.hasMatch()) return hm.captured(1).toInt();

                    if(functionWidths.contains(name)) return functionWidths.value(name);
                    if(name == "dot" || name == "length" || name == "distance" || name == "determinant" || name == "any" || name == "all") return 1;
                    if(name == "cross") return 3;
                    if(name == "texture" || name == "texture2D" || name == "textureLod" || name == "texelFetch") return 4;

                    const QSet<QString> componentWise = {
                        "abs","sin","cos","tan","asin","acos","atan","exp","exp2","log","log2","sqrt","inversesqrt",
                        "floor","ceil","fract","round","trunc","sign","normalize","reflect","refract","min","max","clamp",
                        "mix","step","smoothstep","pow","mod"
                    };
                    if(componentWise.contains(name))
                    {
                        const QString inside = expr.mid(open + 1, close - open - 1);
                        int width = 1;
                        for(const QString& arg : splitTopLevelArgs(inside)) width = qMax(width, inferWidth(arg, contextPos));
                        return width;
                    }
                }
            }

            // Top-level arithmetic/bitwise operations inherit the widest operand.
            QStringList pieces;
            depth = 0;
            int start = 0;
            bool foundOperator = false;
            for(int i = 0; i < expr.size(); ++i)
            {
                const QChar c = expr[i];
                if(c == '(' || c == '[' || c == '{') ++depth;
                else if(c == ')' || c == ']' || c == '}') --depth;
                else if(depth == 0)
                {
                    int opLen = 0;
                    const QString two = expr.mid(i, 2);
                    if(two == "<<" || two == ">>") opLen = 2;
                    else if(c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '&' || c == '|' || c == '^') opLen = 1;
                    if(opLen > 0)
                    {
                        const QString left = expr.mid(start, i - start).trimmed();
                        if(!left.isEmpty()) pieces << left;
                        i += opLen - 1;
                        start = i + 1;
                        foundOperator = true;
                    }
                }
            }
            if(foundOperator)
            {
                const QString last = expr.mid(start).trimmed();
                if(!last.isEmpty()) pieces << last;
                int width = 1;
                for(const QString& piece : pieces) width = qMax(width, inferWidth(piece, contextPos));
                return width;
            }

            // Matrix constructors embedded in otherwise simple expressions imply the
            // result width used by GLSL vector*matrix forms (matCxR -> vecC).
            QRegularExpression matrixAnywhereRe("\\bmat([234])(?:x[234])?\\s*\\(");
            const auto ma = matrixAnywhereRe.match(expr);
            if(ma.hasMatch()) return ma.captured(1).toInt();

            // Last-resort identifier scan, respecting scalar and multi-component swizzles.
            QRegularExpression idRe("\\b([A-Za-z_]\\w*)\\b");
            auto idIt = idRe.globalMatch(expr);
            int width = 1;
            while(idIt.hasNext())
            {
                const auto im = idIt.next();
                const QString name = im.captured(1);
                const int declaredWidth = symbolWidthAt(name, contextPos);
                if(declaredWidth <= 1) continue;
                int after = im.capturedEnd(1);
                while(after < expr.size() && expr[after].isSpace()) ++after;
                if(after < expr.size() && expr[after] == '.')
                {
                    ++after;
                    while(after < expr.size() && expr[after].isSpace()) ++after;
                    int end = after;
                    while(end < expr.size() && QString("xyzwrgba").contains(expr[end])) ++end;
                    if(end > after) width = qMax(width, end - after);
                    continue;
                }
                width = qMax(width, declaredWidth);
            }
            return width;
        };

        bool changed = false;
        for(const ConstructorInfo& info : constructors)
        {
            const QString functionName = QString::fromLatin1(info.glslName);
            const QString helperStem = QString::fromLatin1(info.helperStem);
            int searchFrom = 0;
            while(searchFrom < source.size())
            {
                int pos = source.indexOf(functionName, searchFrom, Qt::CaseSensitive);
                if(pos < 0) break;
                const bool leftOk = pos == 0 || !(source[pos-1].isLetterOrNumber() || source[pos-1] == '_');
                int afterName = pos + functionName.size();
                const bool rightOk = afterName >= source.size() || !(source[afterName].isLetterOrNumber() || source[afterName] == '_');
                if(!leftOk || !rightOk) { searchFrom = afterName; continue; }
                while(afterName < source.size() && source[afterName].isSpace()) ++afterName;
                if(afterName >= source.size() || source[afterName] != '(') { searchFrom = afterName; continue; }

                int depth = 0;
                int close = -1;
                int topLevelCommas = 0;
                for(int i = afterName; i < source.size(); ++i)
                {
                    const QChar c = source[i];
                    if(c == '(' || c == '[' || c == '{') ++depth;
                    else if(c == ')' || c == ']' || c == '}')
                    {
                        --depth;
                        if(depth == 0) { close = i; break; }
                    }
                    else if(c == ',' && depth == 1) ++topLevelCommas;
                }
                if(close < 0) break;

                if(topLevelCommas == 0)
                {
                    const QString inside = source.mid(afterName + 1, close - afterName - 1).trimmed();
                    if(!inside.isEmpty())
                    {
                        const int srcWidth = qBound(1, inferWidth(inside, pos), 4);
                        QString replacement;
                        const QString targetType = QString("%1%2")
                            .arg(QString::fromLatin1(info.hlslVectorStem)).arg(info.width);
                        const QString sourceVectorType = QString("%1%2")
                            .arg(QString::fromLatin1(info.hlslVectorStem)).arg(srcWidth);
                        if(srcWidth == 1)
                        {
                            // Directly splat simple scalar expressions. Keep the
                            // one-evaluation helper for calls/expressions that may
                            // have side effects.
                            const bool simpleScalar = QRegularExpression(
                                R"(^[+\-]?(?:[A-Za-z_]\w*(?:\.[xyzwrgba]{1,4})?|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+\-]?\d+)?[fFuU]?)$)")
                                .match(inside.trimmed()).hasMatch();
                            if(simpleScalar)
                            {
                                QStringList splat;
                                for(int component = 0; component < info.width; ++component)
                                    splat << QString("(%1)(%2)").arg(QString::fromLatin1(info.hlslScalar), inside);
                                replacement = QString("%1(%2)").arg(targetType, splat.join(", "));
                            }
                            else
                            {
                                replacement = QString("%1_S((%2)(%3))")
                                                  .arg(helperStem, QString::fromLatin1(info.hlslScalar), inside);
                            }
                        }
                        else if(srcWidth == info.width)
                        {
                            replacement = QString("(%1)(%2)").arg(targetType, inside);
                        }
                        else if(srcWidth < info.width)
                        {
                            QStringList args;
                            args << QString("(%1)(%2)").arg(sourceVectorType, inside);
                            for(int component = srcWidth; component < info.width; ++component)
                                args << (QString::fromLatin1(info.hlslScalar) == "bool" ? "false" : "0");
                            replacement = QString("%1(%2)").arg(targetType, args.join(", "));
                        }
                        else
                        {
                            static const char* swizzles[] = {"", "x", "xy", "xyz", "xyzw"};
                            replacement = QString("((%1)(%2)).%3")
                                .arg(sourceVectorType, inside, QString::fromLatin1(swizzles[info.width]));
                        }
                        source.replace(pos, close - pos + 1, replacement);
                        // Restart at the replacement site. The outer constructor may
                        // have contained another one-argument constructor of the same
                        // GLSL type (for example vec3(dot(c, vec3(0.33)))). The old
                        // forward-only scan skipped that nested constructor because it
                        // now sits before the end of the newly inserted helper call.
                        searchFrom = pos;
                        changed = true;
                        continue;
                    }
                }
                searchFrom = close + 1;
            }
        }

        if(changed)
            notes << "Lowered GLSL one-argument vector constructors to direct FXC-safe HLSL where possible; side-effecting scalar splats retain a one-evaluation compatibility helper.";
        return source;
    }

    QString stripGlslComments(QString source, QStringList& notes) const
    {
        QString out;
        out.reserve(source.size());
        bool inLineComment = false;
        bool inBlockComment = false;
        bool inString = false;
        QChar quote;
        bool escaped = false;
        bool removed = false;

        for(int i = 0; i < source.size(); ++i)
        {
            const QChar c = source[i];
            const QChar next = (i + 1 < source.size()) ? source[i + 1] : QChar();

            if(inLineComment)
            {
                removed = true;
                if(c == '\n')
                {
                    inLineComment = false;
                    out += c;
                }
                continue;
            }

            if(inBlockComment)
            {
                removed = true;
                if(c == '*' && next == '/')
                {
                    inBlockComment = false;
                    ++i;
                    continue;
                }
                // Keep line structure readable and compiler line numbers useful.
                if(c == '\n') out += c;
                continue;
            }

            if(inString)
            {
                out += c;
                if(escaped) escaped = false;
                else if(c == '\\') escaped = true;
                else if(c == quote) inString = false;
                continue;
            }

            if(c == '"' || c == '\'')
            {
                inString = true;
                quote = c;
                out += c;
                continue;
            }
            if(c == '/' && next == '/')
            {
                inLineComment = true;
                removed = true;
                ++i;
                continue;
            }
            if(c == '/' && next == '*')
            {
                // A block comment can legally separate two tokens (foo/*x*/bar).
                // Keep one separator so stripping it cannot accidentally merge code.
                if(!out.isEmpty() && !out.back().isSpace()) out += ' ';
                inBlockComment = true;
                removed = true;
                ++i;
                continue;
            }
            out += c;
        }

        if(removed)
            notes << "Removed GLSL comments/commented-out code from the generated shader.";
        return out;
    }

    int glslSkipSpacesForward(const QString& source, int pos) const
    {
        while(pos < source.size() && source[pos].isSpace()) ++pos;
        return pos;
    }

    int glslSkipSpacesBackward(const QString& source, int pos) const
    {
        while(pos >= 0 && source[pos].isSpace()) --pos;
        return pos;
    }

    int glslFindMatchingForward(const QString& source, int openPos) const
    {
        if(openPos < 0 || openPos >= source.size()) return -1;
        const QChar open = source[openPos];
        QChar close;
        if(open == '(') close = ')';
        else if(open == '[') close = ']';
        else if(open == '{') close = '}';
        else return -1;

        int depth = 0;
        for(int i = openPos; i < source.size(); ++i)
        {
            if(source[i] == open) ++depth;
            else if(source[i] == close)
            {
                --depth;
                if(depth == 0) return i;
            }
        }
        return -1;
    }

    int glslFindMatchingBackward(const QString& source, int closePos) const
    {
        if(closePos < 0 || closePos >= source.size()) return -1;
        const QChar close = source[closePos];
        QChar open;
        if(close == ')') open = '(';
        else if(close == ']') open = '[';
        else if(close == '}') open = '{';
        else return -1;

        int depth = 0;
        for(int i = closePos; i >= 0; --i)
        {
            if(source[i] == close) ++depth;
            else if(source[i] == open)
            {
                --depth;
                if(depth == 0) return i;
            }
        }
        return -1;
    }

    // Returns the first character after a GLSL/HLSL primary expression. This is
    // intentionally an expression-aware scanner rather than a regex so matrix
    // multiplication can safely consume constructors such as float3(uv, -1.0),
    // nested calls, parenthesized expressions, swizzles and array indexing.
    int glslPrimaryEnd(const QString& source, int start) const
    {
        int pos = glslSkipSpacesForward(source, start);
        if(pos >= source.size()) return -1;

        // Unary operators belong to the following primary expression.
        while(pos < source.size() && (source[pos] == '+' || source[pos] == '-' || source[pos] == '!' || source[pos] == '~'))
            pos = glslSkipSpacesForward(source, pos + 1);
        if(pos >= source.size()) return -1;

        if(source[pos] == '(')
        {
            const int close = glslFindMatchingForward(source, pos);
            if(close < 0) return -1;
            pos = close + 1;
        }
        else
        {
            const int tokenStart = pos;
            if(source[pos].isLetter() || source[pos] == '_')
            {
                ++pos;
                while(pos < source.size() && (source[pos].isLetterOrNumber() || source[pos] == '_')) ++pos;
            }
            else if(source[pos].isDigit() || source[pos] == '.')
            {
                ++pos;
                while(pos < source.size() && (source[pos].isLetterOrNumber() || source[pos] == '.' || source[pos] == '+' || source[pos] == '-'))
                {
                    // Stop +/- unless it is part of an exponent.
                    if((source[pos] == '+' || source[pos] == '-') && pos > tokenStart && source[pos-1].toLower() != 'e') break;
                    ++pos;
                }
            }
            else return -1;

            int callPos = glslSkipSpacesForward(source, pos);
            if(callPos < source.size() && source[callPos] == '(')
            {
                const int close = glslFindMatchingForward(source, callPos);
                if(close < 0) return -1;
                pos = close + 1;
            }
        }

        // Include postfix indexing, calls, and swizzles/member access.
        while(true)
        {
            int next = glslSkipSpacesForward(source, pos);
            if(next < source.size() && source[next] == '[')
            {
                const int close = glslFindMatchingForward(source, next);
                if(close < 0) break;
                pos = close + 1;
                continue;
            }
            if(next < source.size() && source[next] == '(')
            {
                const int close = glslFindMatchingForward(source, next);
                if(close < 0) break;
                pos = close + 1;
                continue;
            }
            if(next < source.size() && source[next] == '.')
            {
                int member = glslSkipSpacesForward(source, next + 1);
                if(member >= source.size() || !(source[member].isLetter() || source[member] == '_')) break;
                ++member;
                while(member < source.size() && (source[member].isLetterOrNumber() || source[member] == '_')) ++member;
                pos = member;
                continue;
            }
            break;
        }
        return pos;
    }

    int glslPrimaryStart(const QString& source, int endInclusive) const
    {
        int pos = glslSkipSpacesBackward(source, endInclusive);
        if(pos < 0) return -1;

        // Peel postfix member names first (foo.xyz, call().xyz).
        while(pos >= 0 && (source[pos].isLetterOrNumber() || source[pos] == '_')) --pos;
        if(pos >= 0 && source[pos] == '.')
        {
            return glslPrimaryStart(source, pos - 1);
        }

        pos = glslSkipSpacesBackward(source, endInclusive);
        if(pos < 0) return -1;

        if(source[pos] == ')' || source[pos] == ']')
        {
            const int open = glslFindMatchingBackward(source, pos);
            if(open < 0) return -1;
            int start = open;
            int before = glslSkipSpacesBackward(source, open - 1);
            if(before >= 0 && (source[before].isLetterOrNumber() || source[before] == '_'))
            {
                while(before >= 0 && (source[before].isLetterOrNumber() || source[before] == '_')) --before;
                start = before + 1;
            }
            return start;
        }

        if(source[pos].isLetterOrNumber() || source[pos] == '_' || source[pos] == '.')
        {
            while(pos >= 0 && (source[pos].isLetterOrNumber() || source[pos] == '_' || source[pos] == '.')) --pos;
            return pos + 1;
        }
        return -1;
    }

    bool glslPrimaryIsVector(const QString& source, int start, int endExclusive, const QSet<QString>& vectorNames) const
    {
        if(start < 0 || endExclusive <= start || endExclusive > source.size()) return false;
        QString expr = source.mid(start, endExclusive - start).trimmed();
        if(expr.isEmpty()) return false;

        while(!expr.isEmpty() && (expr[0] == '+' || expr[0] == '-' || expr[0] == '!' || expr[0] == '~'))
            expr = expr.mid(1).trimmed();
        while(expr.startsWith('('))
        {
            const int close = glslFindMatchingForward(expr, 0);
            if(close != expr.size() - 1) break;
            expr = expr.mid(1, expr.size() - 2).trimmed();
        }
        if(expr.isEmpty()) return false;

        // A terminal vector swizzle determines the result width regardless of
        // the base identifier's type. .x is scalar; .xy/.xyz/.xyzw are vectors.
        const auto swizzle = QRegularExpression("\\.([xyzwrgba]{1,4})$").match(expr);
        if(swizzle.hasMatch())
            return swizzle.captured(1).size() >= 2;

        const auto match = QRegularExpression("^([A-Za-z_]\\w*)").match(expr);
        if(!match.hasMatch()) return false;
        const QString ident = match.captured(1);
        if(QRegularExpression("^(?:float|int|uint|bool)[234]$").match(ident).hasMatch() ||
           QRegularExpression("^GLSL_[IUB]?VEC[234]$").match(ident).hasMatch())
            return true;
        return vectorNames.contains(ident);
    }

    QString convertGlslVectorEquality(QString source, const QSet<QString>& vectorNames, QStringList& notes) const
    {
        bool changed = false;
        int searchFrom = 0;
        while(searchFrom < source.size())
        {
            const int eqPos = source.indexOf("==", searchFrom, Qt::CaseSensitive);
            const int nePos = source.indexOf("!=", searchFrom, Qt::CaseSensitive);
            int opPos = -1;
            bool isNotEqual = false;
            if(eqPos < 0) { opPos = nePos; isNotEqual = true; }
            else if(nePos < 0) { opPos = eqPos; }
            else if(nePos < eqPos) { opPos = nePos; isNotEqual = true; }
            else { opPos = eqPos; }
            if(opPos < 0) break;

            const int lhsEnd = glslSkipSpacesBackward(source, opPos - 1);
            const int lhsStart = glslPrimaryStart(source, lhsEnd);
            const int rhsStart = glslSkipSpacesForward(source, opPos + 2);
            const int rhsEnd = glslPrimaryEnd(source, rhsStart);
            if(lhsStart < 0 || lhsEnd < lhsStart || rhsEnd <= rhsStart)
            {
                searchFrom = opPos + 2;
                continue;
            }

            const bool lhsVector = glslPrimaryIsVector(source, lhsStart, lhsEnd + 1, vectorNames);
            const bool rhsVector = glslPrimaryIsVector(source, rhsStart, rhsEnd, vectorNames);
            if(!lhsVector && !rhsVector)
            {
                searchFrom = opPos + 2;
                continue;
            }

            const QString lhs = source.mid(lhsStart, lhsEnd - lhsStart + 1).trimmed();
            const QString rhs = source.mid(rhsStart, rhsEnd - rhsStart).trimmed();
            // GLSL vector ==/!= returns one scalar bool. HLSL comparisons are
            // component-wise, so reduce them explicitly.
            const QString replacement = isNotEqual
                ? QString("any(%1 != %2)").arg(lhs, rhs)
                : QString("all(%1 == %2)").arg(lhs, rhs);
            source.replace(lhsStart, rhsEnd - lhsStart, replacement);
            searchFrom = lhsStart + replacement.size();
            changed = true;
        }

        if(changed)
            notes << "Converted GLSL scalar vector equality semantics to HLSL all()/any() reductions.";
        return source;
    }

    bool glslPrimaryIsMatrix(const QString& source, int start, int endExclusive, const QSet<QString>& matrixNames) const
    {
        if(start < 0 || endExclusive <= start || endExclusive > source.size()) return false;
        QString expr = source.mid(start, endExclusive - start).trimmed();
        if(expr.isEmpty()) return false;

        // Unary +/- preserves matrix type.
        while(!expr.isEmpty() && (expr[0] == '+' || expr[0] == '-'))
            expr = expr.mid(1).trimmed();

        // Peel fully enclosing parentheses.
        while(expr.startsWith('('))
        {
            const int close = glslFindMatchingForward(expr, 0);
            if(close != expr.size() - 1) break;
            expr = expr.mid(1, expr.size() - 2).trimmed();
        }
        if(expr.isEmpty()) return false;

        QRegularExpression identRe("^([A-Za-z_]\\w*)");
        const auto match = identRe.match(expr);
        if(!match.hasMatch()) return false;
        const QString ident = match.captured(1);

        // HLSL matrix constructors after token conversion. The converter keeps
        // GLSL's CxR spelling as floatCxR because that HLSL object represents
        // the transpose of the GLSL matrix (matching our mul operand reversal).
        if(QRegularExpression("^float[234]x[234]$").match(ident).hasMatch() ||
           QRegularExpression("^GLSL_MAT[234]$").match(ident).hasMatch())
            return true;

        // transpose(matrix) is matrix-valued even though "transpose" itself was
        // not declared with a matN return type in user GLSL.
        if(ident == "transpose" || ident == "GLSL_INVERSE")
            return true;

        // A rewritten matrix-matrix product remains matrix-valued and may be the
        // left or right operand of a following GLSL chain operation. Preserve that
        // fact through generated mul(A,B) calls; mul(matrix,vector) and
        // mul(vector,matrix) deliberately remain non-matrix results.
        if(ident == "mul")
        {
            const int open = expr.indexOf('(', match.capturedEnd(1));
            const int close = open >= 0 ? glslFindMatchingForward(expr, open) : -1;
            if(open >= 0 && close == expr.size() - 1)
            {
                int comma = -1;
                int paren = 0, bracket = 0;
                for(int i = open + 1; i < close; ++i)
                {
                    if(expr[i] == '(') ++paren;
                    else if(expr[i] == ')') --paren;
                    else if(expr[i] == '[') ++bracket;
                    else if(expr[i] == ']') --bracket;
                    else if(expr[i] == ',' && paren == 0 && bracket == 0)
                    {
                        comma = i;
                        break;
                    }
                }
                if(comma > open + 1 && comma < close - 1)
                {
                    const bool firstMatrix = glslPrimaryIsMatrix(
                        expr, open + 1, comma, matrixNames);
                    const bool secondMatrix = glslPrimaryIsMatrix(
                        expr, comma + 1, close, matrixNames);
                    return firstMatrix && secondMatrix;
                }
            }
            return false;
        }

        const auto memberMatch =
            QRegularExpression("\\.([A-Za-z_]\\w*)\\s*$").match(expr);
        if(memberMatch.hasMatch() && matrixNames.contains(memberMatch.captured(1)))
            return true;

        if(!matrixNames.contains(ident))
            return false;

        // Short identifiers are routinely reused with different numeric types in
        // shader-golfed GLSL and overloaded helper families.  A global matrix-name
        // set alone is therefore not enough: `mat3 helper(mat3 x)` elsewhere must
        // not make the `x` in `vec3 permute(vec3 x)` matrix-valued.  Prefer the
        // nearest concrete numeric declaration before this expression.
        {
            const int limit = qBound(0, start, source.size());
            const QString prefix = source.left(limit);
            const QString escapedIdent = QRegularExpression::escape(ident);
            const QRegularExpression declRe(
                QString("\\b(float[234]x[234]|vec1|(?:float|int|uint|bool)(?:[234])?)\\s+%1\\b")
                    .arg(escapedIdent));
            auto it = declRe.globalMatch(prefix);
            QString nearestType;
            while(it.hasNext())
                nearestType = it.next().captured(1);

            if(!nearestType.isEmpty())
                return QRegularExpression("^float[234]x[234]$").match(nearestType).hasMatch();
        }

        // Indexing a matrix (m[0]) yields a vector, not another matrix.
        int afterIdent = match.capturedEnd(1);
        while(afterIdent < expr.size() && expr[afterIdent].isSpace()) ++afterIdent;
        if(afterIdent < expr.size() && expr[afterIdent] == '[')
            return false;

        return true;
    }

    QString convertGlslMatrixMultiplication(QString source, const QSet<QString>& matrixNames, const QSet<QString>& vectorNames, const QSet<QString>& scalarNames, QStringList& notes) const
    {
        bool changed = false;

        // GLSL matrix constructors are column-oriented. The directly translated
        // HLSL floatCxR constructors are row-oriented, so they numerically hold
        // the transpose of the GLSL matrix. Reverse mul() operand order to keep
        // the same result:
        //   GLSL M * v  -> HLSL mul(v, M)
        //   GLSL v * M  -> HLSL mul(M, v)
        //   GLSL A * B  -> HLSL mul(B, A)

        auto primaryText = [&](int start, int endExclusive)
        {
            return source.mid(start, endExclusive - start).trimmed();
        };

        // Do not rewrite operators inside preprocessor directives. Macro bodies
        // have their own parameter typing and can reuse short names that are
        // matrices elsewhere in the shader; applying source-level matrix typing
        // to `#define helper(a,b) ((a)*(b))` can silently corrupt the macro.
        auto isPreprocessorPosition = [&](int position)
        {
            if(position < 0 || position >= source.size()) return false;
            int lineStart = source.lastIndexOf('\n', position);
            lineStart = (lineStart < 0) ? 0 : lineStart + 1;
            int first = lineStart;
            while(first < source.size() && first <= position &&
                  (source[first] == ' ' || source[first] == '\t'))
                ++first;
            return first <= position && first < source.size() && source[first] == '#';
        };

        // Short shader-golf identifiers (a, b, p, q, etc.) are frequently reused
        // with different types in different functions. The global scalar/vector name
        // sets are useful as a fallback, but matrix multiplication must prefer the
        // nearest concrete declaration before the expression or a vec2 parameter named
        // `a` can be mistaken for an unrelated scalar `a` declared elsewhere.
        auto nearestNumericKind = [&](const QString& name, int contextPos)
        {
            enum { UnknownKind = 0, ScalarKind = 1, VectorKind = 2, MatrixKind = 3 };
            const int limit = qBound(0, contextPos, source.size());
            const QString prefix = source.left(limit);
            const QString escapedName = QRegularExpression::escape(name);
            const QRegularExpression declRe(
                QString("\\b(vec1|(?:float|int|uint|bool)(?:[234])?|float[234]x[234])\\s+%1\\b")
                    .arg(escapedName));
            auto it = declRe.globalMatch(prefix);
            int kind = UnknownKind;
            int nearestDeclaration = -1;
            while(it.hasNext())
            {
                const auto declaration = it.next();
                const QString type = declaration.captured(1);
                if(QRegularExpression("^float[234]x[234]$").match(type).hasMatch())
                    kind = MatrixKind;
                else if(QRegularExpression("^(?:float|int|uint|bool)[234]$").match(type).hasMatch())
                    kind = VectorKind;
                else
                    kind = ScalarKind;
                nearestDeclaration = declaration.capturedStart(0);
            }

            // Include later names in comma declaration lists (`float2 a=..., b,
            // c=...;`). The simple declaration regex above intentionally handles
            // parameters and first declarators; real compact shaders frequently pass
            // a later declarator into a type-sensitive macro.
            const QRegularExpression declarationListRe(
                "\\b(?:(?:const|static)\\s+)*(vec1|(?:float|int|uint|bool)(?:[234])?|"
                "float[234]x[234])\\s+([^;{}]+);");
            auto declarationListIt = declarationListRe.globalMatch(prefix);
            while(declarationListIt.hasNext())
            {
                const auto declaration = declarationListIt.next();
                const QString declarators = declaration.captured(2);
                QStringList parts;
                int paren = 0, bracket = 0, start = 0;
                for(int i = 0; i < declarators.size(); ++i)
                {
                    const QChar c = declarators[i];
                    if(c == '(') ++paren;
                    else if(c == ')') --paren;
                    else if(c == '[') ++bracket;
                    else if(c == ']') --bracket;
                    else if(c == ',' && paren == 0 && bracket == 0)
                    {
                        parts << declarators.mid(start, i - start).trimmed();
                        start = i + 1;
                    }
                }
                parts << declarators.mid(start).trimmed();

                bool declaresName = false;
                for(QString part : parts)
                {
                    part = part.section('=', 0, 0).trimmed();
                    const auto nameMatch = QRegularExpression("^([A-Za-z_]\\w*)").match(part);
                    if(nameMatch.hasMatch() && nameMatch.captured(1) == name)
                    {
                        declaresName = true;
                        break;
                    }
                }
                if(!declaresName || declaration.capturedStart(0) < nearestDeclaration)
                    continue;

                const QString type = declaration.captured(1);
                if(QRegularExpression("^float[234]x[234]$").match(type).hasMatch())
                    kind = MatrixKind;
                else if(QRegularExpression("^(?:float|int|uint|bool)[234]$").match(type).hasMatch())
                    kind = VectorKind;
                else
                    kind = ScalarKind;
                nearestDeclaration = declaration.capturedStart(0);
            }
            return kind;
        };

        auto primaryIsScalar = [&](int start, int endExclusive)
        {
            QString expr = primaryText(start, endExclusive);
            while(expr.startsWith('('))
            {
                const int close = glslFindMatchingForward(expr, 0);
                if(close != expr.size() - 1) break;
                expr = expr.mid(1, expr.size() - 2).trimmed();
            }
            if(QRegularExpression("^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?[fF]?$").match(expr).hasMatch())
                return true;
            if(expr == "true" || expr == "false")
                return true;
            if(QRegularExpression("^(?:float|int|uint|bool)\\s*\\(").match(expr).hasMatch())
                return true;

            const auto idm = QRegularExpression("^([A-Za-z_]\\w*)(?:\\.([xyzwrgba]))?$").match(expr);
            if(idm.hasMatch())
            {
                if(!idm.captured(2).isEmpty()) return true;
                const int declaredKind = nearestNumericKind(idm.captured(1), start);
                if(declaredKind != 0) return declaredKind == 1;
                return scalarNames.contains(idm.captured(1)) && !vectorNames.contains(idm.captured(1));
            }
            return false;
        };

        auto primaryIsVector = [&](int start, int endExclusive)
        {
            QString expr = primaryText(start, endExclusive);
            while(expr.startsWith('('))
            {
                const int close = glslFindMatchingForward(expr, 0);
                if(close != expr.size() - 1) break;
                expr = expr.mid(1, expr.size() - 2).trimmed();
            }
            if(QRegularExpression("^(?:float|int|uint|bool)[234]\\s*\\(").match(expr).hasMatch() ||
               QRegularExpression("^GLSL_(?:I|U|B)?VEC[234]_").match(expr).hasMatch())
                return true;

            const auto idm = QRegularExpression("^([A-Za-z_]\\w*)(?:\\.([xyzwrgba]{2,4}))?$").match(expr);
            if(idm.hasMatch())
            {
                if(!idm.captured(2).isEmpty()) return true;
                const int declaredKind = nearestNumericKind(idm.captured(1), start);
                if(declaredKind != 0) return declaredKind == 2;
                return vectorNames.contains(idm.captured(1)) && !scalarNames.contains(idm.captured(1));
            }
            return false;
        };

        auto stripExpressionParens = [&](QString expr)
        {
            expr = expr.trimmed();
            while(expr.startsWith('('))
            {
                const int close = glslFindMatchingForward(expr, 0);
                if(close != expr.size() - 1) break;
                expr = expr.mid(1, expr.size() - 2).trimmed();
            }
            return expr;
        };

        auto splitExpressionArgs = [](const QString& text) -> QStringList
        {
            QStringList args;
            int paren = 0, bracket = 0, brace = 0, start = 0;
            for(int i = 0; i < text.size(); ++i)
            {
                const QChar c = text[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                {
                    args << text.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            args << text.mid(start).trimmed();
            if(args.size() == 1 && args[0].isEmpty()) args.clear();
            return args;
        };

        enum NumericKind { UnknownKind = 0, ScalarKind = 1, VectorKind = 2, MatrixKind = 3 };
        std::function<int(QString,int)> expressionNumericKind;
        expressionNumericKind = [&](QString expr, int contextPos) -> int
        {
            expr = stripExpressionParens(expr);
            if(expr.isEmpty()) return UnknownKind;
            while(!expr.isEmpty() && (expr[0] == '+' || expr[0] == '-'))
                expr = stripExpressionParens(expr.mid(1));
            if(expr.isEmpty()) return UnknownKind;

            if(QRegularExpression("^(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?[fF]?$")
                   .match(expr).hasMatch() || expr == "true" || expr == "false")
                return ScalarKind;
            if(QRegularExpression("^(?:float|int|uint|bool)\\s*\\(").match(expr).hasMatch())
                return ScalarKind;
            if(QRegularExpression("^(?:float|int|uint|bool)[234]\\s*\\(").match(expr).hasMatch() ||
               QRegularExpression("^GLSL_(?:I|U|B)?VEC[234]_").match(expr).hasMatch())
                return VectorKind;
            if(QRegularExpression("^(?:float[234]x[234]|GLSL_MAT[234])\\s*\\(").match(expr).hasMatch())
                return MatrixKind;

            // Carry known scalar/vector/matrix kinds through ordinary arithmetic.
            // This is used only to prove macro invocation argument types; any
            // unknown or conflicting expression keeps the macro definition intact.
            auto inferTopLevelOperator = [&](const QString& operators) -> int
            {
                int paren = 0, bracket = 0;
                for(int i = expr.size() - 1; i >= 0; --i)
                {
                    const QChar c = expr[i];
                    if(c == ')') { ++paren; continue; }
                    if(c == '(') { if(paren > 0) --paren; continue; }
                    if(c == ']') { ++bracket; continue; }
                    if(c == '[') { if(bracket > 0) --bracket; continue; }
                    if(paren != 0 || bracket != 0 || !operators.contains(c)) continue;
                    if((c == '+' || c == '-') && i > 0)
                    {
                        int previous = i - 1;
                        while(previous >= 0 && expr[previous].isSpace()) --previous;
                        if(previous < 0 || QString("eE([,{+-*/%").contains(expr[previous])) continue;
                    }
                    const QString left = expr.left(i).trimmed();
                    const QString right = expr.mid(i + 1).trimmed();
                    if(left.isEmpty() || right.isEmpty()) continue;
                    const int leftKind = expressionNumericKind(left, contextPos);
                    const int rightKind = expressionNumericKind(right, contextPos);
                    if(leftKind == UnknownKind || rightKind == UnknownKind) continue;
                    if(leftKind == rightKind) return leftKind;
                    if(leftKind == ScalarKind) return rightKind;
                    if(rightKind == ScalarKind) return leftKind;
                    return UnknownKind;
                }
                return UnknownKind;
            };
            int arithmeticKind = inferTopLevelOperator("+-");
            if(arithmeticKind == UnknownKind) arithmeticKind = inferTopLevelOperator("*/%");
            if(arithmeticKind != UnknownKind) return arithmeticKind;

            const auto identifier = QRegularExpression(
                "^([A-Za-z_]\\w*)(?:\\s*\\([^)]*\\))?(?:\\.([xyzwrgba]{1,4}))?$")
                .match(expr);
            if(identifier.hasMatch())
            {
                if(!identifier.captured(2).isEmpty())
                    return identifier.captured(2).size() == 1 ? ScalarKind : VectorKind;
                const QString name = identifier.captured(1);
                const int declaredKind = nearestNumericKind(name, contextPos);
                if(declaredKind != UnknownKind) return declaredKind;
                if(matrixNames.contains(name)) return MatrixKind;
                if(vectorNames.contains(name) && !scalarNames.contains(name)) return VectorKind;
                if(scalarNames.contains(name) && !vectorNames.contains(name)) return ScalarKind;
            }
            return UnknownKind;
        };

        // Keep generic preprocessor parameters opaque unless every concrete call
        // site proves the relevant parameter has one stable vector/matrix kind.
        // When the opposite operand is an explicit matrix constructor, that proof
        // is sufficient to lower the macro body without reviving the old unsafe
        // global-name heuristic for arbitrary #define expressions.
        struct FunctionMacro
        {
            QString name;
            QStringList params;
            QString body;
            int nameStart = -1;
            int bodyStart = -1;
            int bodyEnd = -1;
        };
        QVector<FunctionMacro> functionMacros;
        const QRegularExpression functionMacroRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\(([^)]*)\\)\\s*(.*)$",
            QRegularExpression::MultilineOption);
        auto functionMacroIt = functionMacroRe.globalMatch(source);
        while(functionMacroIt.hasNext())
        {
            const auto macro = functionMacroIt.next();
            const QString body = macro.captured(3).trimmed();
            const QString parameterText = macro.captured(2).trimmed();
            if(body.isEmpty() || body.endsWith('\\') || parameterText.contains("...") ||
               body.contains("##") ||
               QRegularExpression("(^|[^#])#\\s*[A-Za-z_]\\w*").match(body).hasMatch())
                continue;
            QStringList params;
            for(QString param : parameterText.split(',', Qt::SkipEmptyParts))
                params << param.trimmed();
            functionMacros.push_back({macro.captured(1), params, body,
                                      static_cast<int>(macro.capturedStart(1)),
                                      static_cast<int>(macro.capturedStart(3)),
                                      static_cast<int>(macro.capturedEnd(3))});
        }

        QMap<QString, FunctionMacro> functionMacrosByName;
        for(const FunctionMacro& macro : functionMacros)
            functionMacrosByName.insert(macro.name, macro);

        auto macroHasHiddenInvocation = [&](const FunctionMacro& macro) -> bool
        {
            const QRegularExpression callRe(
                QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(macro.name)));
            auto callIt = callRe.globalMatch(source);
            while(callIt.hasNext())
            {
                const auto call = callIt.next();
                if(call.capturedStart(0) == macro.nameStart) continue;
                if(isPreprocessorPosition(call.capturedStart(0))) return true;
            }
            return false;
        };

        QMap<QString, int> provenMacroParameterKinds;
        auto provenMacroParameterKind = [&](const FunctionMacro& macro, int parameterIndex) -> int
        {
            const QString key = QString("%1:%2").arg(macro.name).arg(parameterIndex);
            const auto cached = provenMacroParameterKinds.constFind(key);
            if(cached != provenMacroParameterKinds.cend()) return cached.value();

            int provenKind = UnknownKind;
            bool sawInvocation = false;
            bool invalid = false;
            const QRegularExpression callRe(
                QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(macro.name)));
            auto callIt = callRe.globalMatch(source);
            while(callIt.hasNext())
            {
                const auto call = callIt.next();
                if(call.capturedStart(0) == macro.nameStart) continue;
                if(isPreprocessorPosition(call.capturedStart(0)))
                {
                    invalid = true;
                    break;
                }
                const int open = source.indexOf('(', call.capturedStart(0));
                const int close = open >= 0 ? glslFindMatchingForward(source, open) : -1;
                if(open < 0 || close < 0) { invalid = true; break; }
                const QStringList args = splitExpressionArgs(source.mid(open + 1, close - open - 1));
                if(args.size() != macro.params.size()) { invalid = true; break; }
                const int kind = expressionNumericKind(args[parameterIndex], call.capturedStart(0));
                if(kind == UnknownKind || kind == ScalarKind) { invalid = true; break; }
                if(provenKind != UnknownKind && provenKind != kind) { invalid = true; break; }
                provenKind = kind;
                sawInvocation = true;
            }
            if(!sawInvocation || invalid) provenKind = UnknownKind;
            provenMacroParameterKinds.insert(key, provenKind);
            return provenKind;
        };

        struct MacroMatrixEdit { int start; int length; QString replacement; };
        QVector<MacroMatrixEdit> macroMatrixEdits;
        QVector<MacroMatrixEdit> callSiteMatrixEdits;
        QSet<QString> callSiteMatrixMacroNames;
        for(const FunctionMacro& macro : functionMacros)
        {
            const bool hasHiddenInvocation = macroHasHiddenInvocation(macro);
            int opPos = source.indexOf('*', macro.bodyStart);
            while(opPos >= macro.bodyStart && opPos < macro.bodyEnd)
            {
                if(opPos + 1 < source.size() && source[opPos + 1] == '=')
                {
                    opPos = source.indexOf('*', opPos + 2);
                    continue;
                }
                const int lhsEnd = glslSkipSpacesBackward(source, opPos - 1);
                const int lhsStart = glslPrimaryStart(source, lhsEnd);
                const int rhsStart = glslSkipSpacesForward(source, opPos + 1);
                const int rhsEnd = glslPrimaryEnd(source, rhsStart);
                if(lhsStart < macro.bodyStart || rhsEnd <= rhsStart || rhsEnd > macro.bodyEnd)
                {
                    opPos = source.indexOf('*', opPos + 1);
                    continue;
                }

                const QString lhs = primaryText(lhsStart, lhsEnd + 1);
                const QString rhs = primaryText(rhsStart, rhsEnd);
                const QString bareLhs = stripExpressionParens(lhs);
                const QString bareRhs = stripExpressionParens(rhs);
                const bool explicitLhsMatrix = QRegularExpression(
                    "^(?:float[234]x[234]|GLSL_MAT[234])\\s*\\(").match(bareLhs).hasMatch();
                const bool explicitRhsMatrix = QRegularExpression(
                    "^(?:float[234]x[234]|GLSL_MAT[234])\\s*\\(").match(bareRhs).hasMatch();
                if(explicitLhsMatrix == explicitRhsMatrix)
                {
                    opPos = source.indexOf('*', opPos + 1);
                    continue;
                }

                const QString other = explicitLhsMatrix ? bareRhs : bareLhs;
                const int parameterIndex = macro.params.indexOf(other);
                if(parameterIndex >= 0 && hasHiddenInvocation)
                {
                    // A use in another macro can later receive a different numeric
                    // kind than the direct calls visible here. Never let a partial
                    // proof authorize a global replacement-body rewrite; expand the
                    // transitive wrapper chain only at concrete, normal-code sites.
                    callSiteMatrixMacroNames.insert(macro.name);
                    opPos = source.indexOf('*', opPos + 1);
                    continue;
                }
                int otherKind = UnknownKind;
                if(parameterIndex >= 0)
                    otherKind = provenMacroParameterKind(macro, parameterIndex);
                else
                    otherKind = expressionNumericKind(other, opPos);
                if(otherKind != VectorKind && otherKind != MatrixKind)
                {
                    opPos = source.indexOf('*', opPos + 1);
                    continue;
                }

                macroMatrixEdits.push_back({lhsStart, rhsEnd - lhsStart,
                                            QString("mul(%1, %2)").arg(rhs, lhs)});
                opPos = source.indexOf('*', rhsEnd);
            }
        }

        int callSiteMatrixMacroExpansionCount = 0;
        if(!callSiteMatrixMacroNames.isEmpty())
        {
            auto bodyCallsAnyMacro = [](const QString& body,
                                        const QSet<QString>& names) -> bool
            {
                for(const QString& name : names)
                {
                    const QRegularExpression callRe(
                        QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(name)));
                    if(callRe.match(body).hasMatch()) return true;
                }
                return false;
            };

            // Include every function-like wrapper that can reach an unsafe macro.
            // Expanding the outermost concrete call exposes the entire chain while
            // leaving every global replacement list untouched.
            bool discoveredWrapper = true;
            while(discoveredWrapper)
            {
                discoveredWrapper = false;
                for(auto macro = functionMacrosByName.cbegin();
                    macro != functionMacrosByName.cend(); ++macro)
                {
                    if(callSiteMatrixMacroNames.contains(macro.key())) continue;
                    if(!bodyCallsAnyMacro(macro.value().body, callSiteMatrixMacroNames)) continue;
                    callSiteMatrixMacroNames.insert(macro.key());
                    discoveredWrapper = true;
                }
            }

            auto expandOneMacro = [](const FunctionMacro& macro,
                                     const QStringList& args) -> QString
            {
                if(args.size() != macro.params.size()) return {};
                QMap<QString, int> parameterIndexes;
                for(int i = 0; i < macro.params.size(); ++i)
                    parameterIndexes.insert(macro.params[i], i);

                QString expanded;
                int pos = 0;
                while(pos < macro.body.size())
                {
                    if(macro.body[pos].isLetter() || macro.body[pos] == '_')
                    {
                        const int start = pos++;
                        while(pos < macro.body.size() &&
                              (macro.body[pos].isLetterOrNumber() || macro.body[pos] == '_'))
                            ++pos;
                        const QString token = macro.body.mid(start, pos - start);
                        const auto parameter = parameterIndexes.constFind(token);
                        if(parameter != parameterIndexes.cend())
                            expanded += QString("(%1)").arg(args[parameter.value()]);
                        else
                            expanded += token;
                        continue;
                    }
                    expanded += macro.body[pos++];
                }
                return expanded;
            };

            std::function<QString(QString,int)> expandMatrixMacroChain;
            expandMatrixMacroChain = [&](QString expression, int depth) -> QString
            {
                if(depth >= 32) return expression;
                int pos = 0;
                while(pos < expression.size())
                {
                    if(!(expression[pos].isLetter() || expression[pos] == '_'))
                    {
                        ++pos;
                        continue;
                    }
                    const int nameStart = pos++;
                    while(pos < expression.size() &&
                          (expression[pos].isLetterOrNumber() || expression[pos] == '_'))
                        ++pos;
                    const QString name = expression.mid(nameStart, pos - nameStart);
                    if(!callSiteMatrixMacroNames.contains(name)) continue;
                    const auto macro = functionMacrosByName.constFind(name);
                    if(macro == functionMacrosByName.cend()) continue;

                    int open = pos;
                    while(open < expression.size() && expression[open].isSpace()) ++open;
                    if(open >= expression.size() || expression[open] != '(') continue;
                    const int close = glslFindMatchingForward(expression, open);
                    if(close < 0) continue;
                    const QStringList args = splitExpressionArgs(
                        expression.mid(open + 1, close - open - 1));
                    QString replacement = expandOneMacro(macro.value(), args);
                    if(replacement.isEmpty())
                    {
                        pos = close + 1;
                        continue;
                    }
                    replacement = expandMatrixMacroChain(replacement, depth + 1);
                    const QString wrappedReplacement = QString("(%1)").arg(replacement);
                    expression.replace(nameStart, close - nameStart + 1,
                                       wrappedReplacement);
                    pos = nameStart + wrappedReplacement.size();
                }
                return expression;
            };

            for(const QString& name : callSiteMatrixMacroNames)
            {
                const auto macro = functionMacrosByName.constFind(name);
                if(macro == functionMacrosByName.cend()) continue;
                const QRegularExpression callRe(
                    QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(name)));
                auto callIt = callRe.globalMatch(source);
                while(callIt.hasNext())
                {
                    const auto call = callIt.next();
                    if(call.capturedStart(0) == macro.value().nameStart ||
                       isPreprocessorPosition(call.capturedStart(0)))
                        continue;
                    const int open = source.indexOf('(', call.capturedStart(0));
                    const int close = open >= 0 ? glslFindMatchingForward(source, open) : -1;
                    if(close < 0) continue;
                    const QStringList args = splitExpressionArgs(
                        source.mid(open + 1, close - open - 1));
                    QString replacement = expandOneMacro(macro.value(), args);
                    if(replacement.isEmpty()) continue;
                    replacement = expandMatrixMacroChain(replacement, 1);
                    const int callStart = static_cast<int>(call.capturedStart(0));
                    callSiteMatrixEdits.push_back({callStart,
                                                   close - callStart + 1,
                                                   QString("(%1)").arg(replacement)});
                }
            }
        }

        std::sort(callSiteMatrixEdits.begin(), callSiteMatrixEdits.end(),
                  [](const MacroMatrixEdit& a, const MacroMatrixEdit& b)
                  {
                      if(a.start != b.start) return a.start < b.start;
                      return a.length > b.length;
                  });
        QVector<MacroMatrixEdit> selectedCallSiteMatrixEdits;
        for(const MacroMatrixEdit& candidate : callSiteMatrixEdits)
        {
            const int candidateEnd = candidate.start + candidate.length;
            bool overlaps = false;
            for(const MacroMatrixEdit& selected : selectedCallSiteMatrixEdits)
            {
                const int selectedEnd = selected.start + selected.length;
                if(candidate.start < selectedEnd && selected.start < candidateEnd)
                {
                    overlaps = true;
                    break;
                }
            }
            if(!overlaps) selectedCallSiteMatrixEdits.push_back(candidate);
        }
        callSiteMatrixMacroExpansionCount = selectedCallSiteMatrixEdits.size();
        macroMatrixEdits += selectedCallSiteMatrixEdits;

        std::sort(macroMatrixEdits.begin(), macroMatrixEdits.end(),
                  [](const MacroMatrixEdit& a, const MacroMatrixEdit& b)
                  {
                      return a.start > b.start;
                  });
        for(const MacroMatrixEdit& edit : macroMatrixEdits)
            source.replace(edit.start, edit.length, edit.replacement);
        if(!macroMatrixEdits.isEmpty())
        {
            changed = true;
            const int globalLoweringCount = macroMatrixEdits.size() -
                                            callSiteMatrixMacroExpansionCount;
            if(globalLoweringCount > 0)
                notes << QString("Applied %1 macro-safe explicit matrix multiplication lowering(s) after proving generic parameter types at call sites.")
                             .arg(globalLoweringCount);
            if(callSiteMatrixMacroExpansionCount > 0)
                notes << QString("Expanded %1 call-site matrix macro invocation(s) through transitive wrappers because hidden macro uses prevented a safe global rewrite.")
                             .arg(callSiteMatrixMacroExpansionCount);
        }

        // Handle left-associative matrix/scalar chains before the ordinary
        // primary-by-primary scanner. Examples seen in real Shadertoy code:
        //     p * 3.0 * m
        //     M * 0.3 * vec3(...)
        // The middle scalar is commutative, but if we only inspect the immediate
        // primary next to the second '*', the matrix relationship is hidden.
        int chainSearch = 0;
        while(chainSearch < source.size())
        {
            const int secondOp = source.indexOf('*', chainSearch);
            if(secondOp < 0) break;
            if(isPreprocessorPosition(secondOp))
            {
                const int lineEnd = source.indexOf('\n', secondOp);
                chainSearch = lineEnd < 0 ? source.size() : lineEnd + 1;
                continue;
            }
            if(secondOp + 1 < source.size() && source[secondOp + 1] == '=')
            {
                chainSearch = secondOp + 2;
                continue;
            }

            const int midEnd = glslSkipSpacesBackward(source, secondOp - 1);
            const int midStart = glslPrimaryStart(source, midEnd);
            if(midStart < 0 || !primaryIsScalar(midStart, midEnd + 1))
            {
                chainSearch = secondOp + 1;
                continue;
            }

            const int firstOp = glslSkipSpacesBackward(source, midStart - 1);
            if(firstOp < 0 || source[firstOp] != '*' ||
               (firstOp > 0 && source[firstOp - 1] == '*'))
            {
                chainSearch = secondOp + 1;
                continue;
            }

            const int leftEnd = glslSkipSpacesBackward(source, firstOp - 1);
            const int leftStart = glslPrimaryStart(source, leftEnd);
            const int rightStart = glslSkipSpacesForward(source, secondOp + 1);
            const int rightEnd = glslPrimaryEnd(source, rightStart);
            if(leftStart < 0 || rightEnd <= rightStart)
            {
                chainSearch = secondOp + 1;
                continue;
            }

            const bool leftMatrix = glslPrimaryIsMatrix(source, leftStart, leftEnd + 1, matrixNames);
            const bool rightMatrix = glslPrimaryIsMatrix(source, rightStart, rightEnd, matrixNames);
            const bool leftVector = primaryIsVector(leftStart, leftEnd + 1);
            const bool rightVector = primaryIsVector(rightStart, rightEnd);
            if(!leftMatrix && !rightMatrix)
            {
                chainSearch = secondOp + 1;
                continue;
            }

            const QString left = primaryText(leftStart, leftEnd + 1);
            const QString scalar = primaryText(midStart, midEnd + 1);
            const QString right = primaryText(rightStart, rightEnd);

            QString replacement;
            if(leftVector && rightMatrix)
                replacement = QString("mul(%1, (%2 * %3))").arg(right, left, scalar);
            else if(leftMatrix && rightVector)
                replacement = QString("(mul(%1, %2) * %3)").arg(right, left, scalar);
            else if(leftMatrix && rightMatrix)
                replacement = QString("(mul(%1, %2) * %3)").arg(right, left, scalar);

            if(replacement.isEmpty())
            {
                chainSearch = secondOp + 1;
                continue;
            }

            source.replace(leftStart, rightEnd - leftStart, replacement);
            chainSearch = 0;
            changed = true;
        }

        // First handle compound vector/matrix multiplication. GLSL allows
        // "v *= mat3(...)" while FXC does not allow vector *= matrix.
        int searchFrom = 0;
        while(searchFrom < source.size())
        {
            const int opPos = source.indexOf("*=", searchFrom, Qt::CaseSensitive);
            if(opPos < 0) break;
            if(isPreprocessorPosition(opPos))
            {
                const int lineEnd = source.indexOf('\n', opPos);
                searchFrom = lineEnd < 0 ? source.size() : lineEnd + 1;
                continue;
            }

            const int lhsEnd = glslSkipSpacesBackward(source, opPos - 1);
            const int lhsStart = glslPrimaryStart(source, lhsEnd);
            const int rhsStart = glslSkipSpacesForward(source, opPos + 2);
            const int rhsEnd = glslPrimaryEnd(source, rhsStart);
            if(lhsStart < 0 || lhsEnd < lhsStart || rhsEnd <= rhsStart)
            {
                searchFrom = opPos + 2;
                continue;
            }

            const bool rhsMatrix = glslPrimaryIsMatrix(source, rhsStart, rhsEnd, matrixNames);
            if(!rhsMatrix)
            {
                searchFrom = opPos + 2;
                continue;
            }

            const QString lhs = source.mid(lhsStart, lhsEnd - lhsStart + 1).trimmed();
            const QString rhs = source.mid(rhsStart, rhsEnd - rhsStart).trimmed();
            const QString replacement = QString("%1 = mul(%2, %1)").arg(lhs, rhs);
            source.replace(lhsStart, rhsEnd - lhsStart, replacement);
            // Restart from the beginning after a matrix rewrite. An outer product
            // can contain another matrix product inside one of its operands; the
            // previous forward-only scan skipped that nested expression after
            // replacing the outer operation. Generated mul() calls contain no '*'
            // operator, so revisiting already-converted text is safe.
            searchFrom = 0;
            changed = true;
        }

        // Then scan ordinary '*' operators. This catches named matrices,
        // matrix-returning functions, transpose(...), and inline constructors
        // such as mat3(...), mat4x2(...), etc.
        searchFrom = 0;
        while(searchFrom < source.size())
        {
            const int opPos = source.indexOf('*', searchFrom);
            if(opPos < 0) break;
            if(isPreprocessorPosition(opPos))
            {
                const int lineEnd = source.indexOf('\n', opPos);
                searchFrom = lineEnd < 0 ? source.size() : lineEnd + 1;
                continue;
            }

            if(opPos + 1 < source.size() && source[opPos + 1] == '=')
            {
                searchFrom = opPos + 2;
                continue;
            }

            const int lhsEnd = glslSkipSpacesBackward(source, opPos - 1);
            const int lhsStart = glslPrimaryStart(source, lhsEnd);
            const int rhsStart = glslSkipSpacesForward(source, opPos + 1);
            const int rhsEnd = glslPrimaryEnd(source, rhsStart);
            if(lhsStart < 0 || lhsEnd < lhsStart || rhsEnd <= rhsStart)
            {
                searchFrom = opPos + 1;
                continue;
            }

            const bool lhsMatrix = glslPrimaryIsMatrix(source, lhsStart, lhsEnd + 1, matrixNames);
            const bool rhsMatrix = glslPrimaryIsMatrix(source, rhsStart, rhsEnd, matrixNames);
            if(!lhsMatrix && !rhsMatrix)
            {
                searchFrom = opPos + 1;
                continue;
            }

            const bool lhsScalar = primaryIsScalar(lhsStart, lhsEnd + 1);
            const bool rhsScalar = primaryIsScalar(rhsStart, rhsEnd);
            // Scalar/matrix products are ordinary component-wise scaling in both
            // languages. Routing them through mul() creates invalid scalar/matrix
            // calls and breaks chains such as 0.3*M*vec3(...).
            if((lhsScalar && rhsMatrix) || (lhsMatrix && rhsScalar))
            {
                searchFrom = opPos + 1;
                continue;
            }

            const QString lhs = source.mid(lhsStart, lhsEnd - lhsStart + 1).trimmed();
            const QString rhs = source.mid(rhsStart, rhsEnd - rhsStart).trimmed();
            const QString replacement = QString("mul(%1, %2)").arg(rhs, lhs);
            source.replace(lhsStart, rhsEnd - lhsStart, replacement);
            // Restart from the beginning after a matrix rewrite. An outer product
            // can contain another matrix product inside one of its operands; the
            // previous forward-only scan skipped that nested expression after
            // replacing the outer operation. Generated mul() calls contain no '*'
            // operator, so revisiting already-converted text is safe.
            searchFrom = 0;
            changed = true;
        }

        if(changed)
            notes << "Converted GLSL square/non-square matrix '*', '*=' operations (including inline constructors and transpose()) to BO3-safe HLSL mul().";
        return source;
    }

    QString routeGlslChannelSamplers(QString source, const QSet<int>& usedChannels, QStringList& notes) const
    {
        bool changed = false;
        for (int channel : usedChannels)
        {
            if (channel < 0 || channel >= kGlslChannelCount) continue;
            const QString ch = QString("iChannel%1").arg(channel);
            const QString samp = QString("glslSampler%1").arg(channel);
            struct Route { const char* from; const char* to; };
            const Route routes[] = {
                {"GLSL_TEXTURE_GRAD", "GLSL_TEXTURE_GRAD_S"},
                {"GLSL_TEXTURE_LEVEL", "GLSL_TEXTURE_LEVEL_S"},
                {"GLSL_TEXTURE_BIAS", "GLSL_TEXTURE_BIAS_S"},
                {"GLSL_TEXTURE", "GLSL_TEXTURE_S"}
            };
            for (const auto& routeItem : routes)
            {
                const QRegularExpression re(QString("\\b%1\\s*\\(\\s*%2\\s*,")
                                                .arg(QString::fromLatin1(routeItem.from)).arg(QRegularExpression::escape(ch)));
                if (re.match(source).hasMatch())
                {
                    source.replace(re, QString("%1(%2, %3,").arg(QString::fromLatin1(routeItem.to)).arg(ch).arg(samp));
                    changed = true;
                }
            }
        }
        if (changed)
            notes << "Routed Shadertoy iChannel0..3 sampling through per-channel samplers.";
        return source;
    }

    QString promoteGlslGlobalConstants(QString source, QStringList& notes) const
    {
        // Legacy FXC treats ordinary global const declarations like external
        // constants. GLSL top-level const data should be shader-private literals,
        // so promote ONLY top-level const tokens to `static const`.
        //
        // Macro-heavy GLSL can place arbitrary braces/parentheses inside #define
        // templates. Those tokens are not active source structure until the macro
        // is expanded, so they must not affect the top-level depth tracker. Mask
        // every preprocessor logical line (including backslash continuations)
        // while preserving source positions before scanning for declarations.
        QString scan = source;
        int lineStart = 0;
        bool continuation = false;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();

            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            const bool directive = continuation || (first < lineEnd && scan[first] == '#');
            if(directive)
            {
                for(int i = lineStart; i < lineEnd; ++i)
                    scan[i] = ' ';
            }

            int tail = lineEnd - 1;
            while(tail >= lineStart && (source[tail] == ' ' || source[tail] == '\t')) --tail;
            continuation = directive && tail >= lineStart && source[tail] == '\\';
            lineStart = lineEnd < scan.size() ? lineEnd + 1 : scan.size();
        }

        // Do not do this line-by-line: a function signature such as
        //     float hash(const float n) {
        // can begin while braceDepth is still zero. Track parentheses as well so
        // function parameters are never rewritten to illegal `static const`.
        // GLSL also permits const values of user-defined struct types. Treat any
        // identifier-like type token as eligible here.
        const QRegularExpression typeAfterConst("^[A-Za-z_]\\w*\\b");

        QVector<int> insertions;
        int braceDepth = 0;
        int parenDepth = 0;
        int bracketDepth = 0;
        bool inString = false;
        QChar quote;
        bool escaped = false;

        auto isIdent = [](QChar c) {
            return c.isLetterOrNumber() || c == '_';
        };

        for(int i = 0; i < scan.size(); ++i)
        {
            const QChar c = scan[i];

            if(inString)
            {
                if(escaped) { escaped = false; continue; }
                if(c == '\\') { escaped = true; continue; }
                if(c == quote) inString = false;
                continue;
            }
            if(c == '"' || c == '\'')
            {
                inString = true;
                quote = c;
                continue;
            }

            if(c == '{') { ++braceDepth; continue; }
            if(c == '}') { if(braceDepth > 0) --braceDepth; continue; }
            if(c == '(') { ++parenDepth; continue; }
            if(c == ')') { if(parenDepth > 0) --parenDepth; continue; }
            if(c == '[') { ++bracketDepth; continue; }
            if(c == ']') { if(bracketDepth > 0) --bracketDepth; continue; }

            if(braceDepth != 0 || parenDepth != 0 || bracketDepth != 0)
                continue;

            if(i + 5 > scan.size() || scan.mid(i, 5) != "const")
                continue;

            const bool leftOk = (i == 0) || !isIdent(scan[i - 1]);
            const bool rightOk = (i + 5 >= scan.size()) || !isIdent(scan[i + 5]);
            if(!leftOk || !rightOk)
                continue;

            // Existing static const is already correct.
            int p = i - 1;
            while(p >= 0 && scan[p].isSpace()) --p;
            int wordEnd = p;
            while(p >= 0 && isIdent(scan[p])) --p;
            const QString previousWord =
                wordEnd >= p + 1 ? scan.mid(p + 1, wordEnd - p) : QString();
            if(previousWord == "static")
                continue;

            int after = i + 5;
            while(after < scan.size() && scan[after].isSpace()) ++after;
            if(!typeAfterConst.match(scan.mid(after)).hasMatch())
                continue;

            insertions.push_back(i);
        }

        for(int n = insertions.size() - 1; n >= 0; --n)
            source.insert(insertions[n], "static ");

        if(!insertions.isEmpty())
            notes << "Converted GLSL top-level const declarations to BO3/FXC-safe static const values with preprocessor-safe scope tracking.";

        return source;
    }

    QString promoteGlslMutableGlobals(QString source, QStringList& notes) const
    {
        // HLSL treats ordinary global variables as external/uniform constants by
        // default. GLSL globals are shader-private storage and may be assigned by
        // helper functions. Prefix non-uniform, non-const top-level DATA
        // declarations with `static` so FXC permits writes.
        //
        // IMPORTANT: do not do this line-by-line. Minified/Shadertoy GLSL commonly
        // places a global declaration and one or more complete functions on the same
        // physical line, e.g.:
        //
        //     float g=0.; float f(float x){g=x;return g;} ...
        //
        // The old line regex missed that form because the line ended in `}` rather
        // than `;`. Scan top-level statements instead, so every global declaration
        // is handled independently of whitespace/minification.

        struct StaticInsertion
        {
            int position = -1;
        };

        QVector<StaticInsertion> insertions;
        bool changed = false;

        const QRegularExpression dataDeclRe(
            "^\\s*((?:(?:lowp|mediump|highp)\\s+)?[A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*(.*);\\s*$",
            QRegularExpression::DotMatchesEverythingOption);

        auto inspectTopLevelStatement = [&](int begin, int endExclusive)
        {
            if(begin < 0 || endExclusive <= begin || endExclusive > source.size())
                return;

            int scanBegin = begin;
            while(scanBegin < endExclusive)
            {
                while(scanBegin < endExclusive && source[scanBegin].isSpace())
                    ++scanBegin;
                if(scanBegin < endExclusive && source[scanBegin] == '#')
                {
                    const int newline = source.indexOf('\n', scanBegin);
                    if(newline < 0 || newline >= endExclusive)
                        return;
                    scanBegin = newline + 1;
                    continue;
                }
                break;
            }

            if(scanBegin >= endExclusive)
                return;

            const QString statement = source.mid(scanBegin, endExclusive - scanBegin);
            const QString trimmed = statement.trimmed();
            if(trimmed.isEmpty())
                return;

            // Never internalize actual GLSL interface/uniform declarations or
            // declarations already explicitly qualified by the author/converter.
            if(trimmed.startsWith("static ") ||
               trimmed.startsWith("const ") ||
               trimmed.startsWith("uniform ") ||
               trimmed.startsWith("in ") ||
               trimmed.startsWith("out ") ||
               trimmed.startsWith("attribute ") ||
               trimmed.startsWith("varying ") ||
               trimmed.startsWith("struct "))
                return;

            const auto match = dataDeclRe.match(statement);
            if(!match.hasMatch())
                return;

            // Function prototypes also end in ';'. A data declaration may continue
            // with '=', '[', ',', or immediately ';', while a prototype begins '('.
            const QString tail = match.captured(3).trimmed();
            if(tail.startsWith('('))
                return;

            // Insert immediately before the GLSL type token, preserving indentation.
            const int typePos = scanBegin + match.capturedStart(1);
            insertions.push_back({typePos});
        };

        int braceDepth = 0;
        int parenDepth = 0;
        int bracketDepth = 0;
        int statementStart = 0;
        bool inString = false;
        QChar stringQuote;
        bool escaped = false;

        for(int i = 0; i < source.size(); ++i)
        {
            const QChar c = source[i];

            // Comments have already been stripped before this pass, but string
            // handling keeps braces/semicolons in macros or literals harmless.
            if(inString)
            {
                if(escaped)
                {
                    escaped = false;
                    continue;
                }
                if(c == '\\')
                {
                    escaped = true;
                    continue;
                }
                if(c == stringQuote)
                    inString = false;
                continue;
            }
            if(c == '"' || c == '\'')
            {
                inString = true;
                stringQuote = c;
                continue;
            }

            if(c == '(') ++parenDepth;
            else if(c == ')' && parenDepth > 0) --parenDepth;
            else if(c == '[') ++bracketDepth;
            else if(c == ']' && bracketDepth > 0) --bracketDepth;

            if(c == '{')
            {
                if(braceDepth == 0)
                {
                    // Anything from statementStart through this opening brace is a
                    // function/struct/control header, not a mutable data declaration.
                    statementStart = i + 1;
                }
                ++braceDepth;
                continue;
            }

            if(c == '}')
            {
                if(braceDepth > 0) --braceDepth;
                if(braceDepth == 0)
                    statementStart = i + 1;
                continue;
            }

            if(braceDepth == 0 && parenDepth == 0 && bracketDepth == 0 && c == ';')
            {
                inspectTopLevelStatement(statementStart, i + 1);
                statementStart = i + 1;
            }
        }

        // Insert from the back so earlier positions remain valid.
        std::sort(insertions.begin(), insertions.end(), [](const StaticInsertion& a, const StaticInsertion& b)
        {
            return a.position > b.position;
        });

        int lastPos = -1;
        for(const StaticInsertion& insertion : insertions)
        {
            if(insertion.position < 0 || insertion.position == lastPos)
                continue;
            source.insert(insertion.position, "static ");
            lastPos = insertion.position;
            changed = true;
        }

        if(changed)
            notes << "Converted GLSL shader-private global variables (including minified same-line declarations) to BO3/FXC-safe static globals.";
        return source;
    }

    QString compactConvertedGlslWhitespace(QString source) const
    {
        // Comment stripping can leave large vertical gaps. Keep at most one empty
        // line so exported shaders stay compact without changing code semantics.
        source.replace(QRegularExpression("\n[ \t]*\n(?:[ \t]*\n)+"), "\n\n");
        return source;
    }

    QString initializeHlslOutParameters(QString source, QStringList& notes) const
    {
        // GLSL `out` parameters are undefined on function entry. Legacy FXC is
        // stricter about definite assignment and can emit X3508 when any member of
        // an out struct (or any component of another out value) is not written on
        // every control-flow path. Initialize non-array out parameters to zero at
        // function entry; authored assignments still overwrite the values normally.
        const QRegularExpression outRe(
            "\\b(out|inout)\\s+([A-Za-z_]\\w*(?:[234](?:x[234])?)?)\\s+([A-Za-z_]\\w*)");
        auto oit = outRe.globalMatch(source);
        QMap<int, QStringList> insertions;

        // Generic macro templates can legitimately contain declarations such as:
        //     #define P(d,e) d e(inout d a,d b){...}
        // Here `d` is a macro parameter, not a concrete GLSL/HLSL type.  Treating
        // it as a type produces invalid injected declarations like:
        //     d _glsl_inout_saved_a = a;
        // Skip every match that belongs to a preprocessor logical line, including
        // backslash-continued directives.  The instantiated macro/function will
        // carry the concrete type when the shader preprocessor expands it.
        const auto isInPreprocessorDirective = [&](int position)
        {
            int lineStart = source.lastIndexOf('\n', qMax(0, position - 1)) + 1;
            int logicalStart = lineStart;
            while(logicalStart > 0)
            {
                const int previousLineEnd = logicalStart - 1;
                const int previousLineStart = source.lastIndexOf('\n', previousLineEnd - 1) + 1;
                const QString previousLine = source.mid(
                    previousLineStart, previousLineEnd - previousLineStart);
                if(!previousLine.trimmed().endsWith('\\'))
                    break;
                logicalStart = previousLineStart;
            }

            int first = logicalStart;
            while(first < source.size() && (source[first] == ' ' || source[first] == '\t'))
                ++first;
            return first < source.size() && source[first] == '#';
        };

        while(oit.hasNext())
        {
            const auto m = oit.next();
            if(isInPreprocessorDirective(m.capturedStart()))
                continue;

            const QString qualifier = m.captured(1);
            const QString typeName = m.captured(2);
            const QString paramName = m.captured(3);

            // Skip out arrays. Their initialization needs element-wise expansion,
            // which is separate from the scalar/vector/matrix/struct case here.
            int afterParam = m.capturedEnd();
            while(afterParam < source.size() && source[afterParam].isSpace())
                ++afterParam;
            if(afterParam < source.size() && source[afterParam] == '[')
                continue;

            // Require this match to be part of a function definition, not a
            // prototype. The first close paren after the parameter must be followed
            // (ignoring whitespace) by the function body's opening brace.
            int closeParen = source.indexOf(')', m.capturedEnd());
            if(closeParen < 0)
                continue;
            int bracePos = closeParen + 1;
            while(bracePos < source.size() && source[bracePos].isSpace())
                ++bracePos;
            if(bracePos >= source.size() || source[bracePos] != '{')
                continue;

            QStringList statements;
            if(qualifier == "inout")
            {
                const QString saved = QString("_glsl_inout_saved_%1").arg(paramName);
                statements << QString("%1 %2 = %3;").arg(typeName, saved, paramName)
                           << QString("%1 = %2;").arg(paramName, saved);
            }
            else
            {
                statements << QString("%1 = (%2)0;").arg(paramName, typeName);
            }

            const int probeEnd = qMin(static_cast<int>(source.size()), bracePos + 384);
            const QString probe = source.mid(bracePos + 1, probeEnd - bracePos - 1);
            bool alreadyPresent = true;
            for(const QString& statement : statements)
                alreadyPresent = alreadyPresent && probe.contains(statement);
            if(alreadyPresent)
                continue;
            for(const QString& statement : statements)
                insertions[bracePos] << statement;
        }

        if(insertions.isEmpty())
            return source;

        const QList<int> positions = insertions.keys();
        for(auto it = positions.crbegin(); it != positions.crend(); ++it)
        {
            const int bracePos = *it;
            QStringList uniqueStatements;
            for(const QString& statement : insertions.value(bracePos))
            {
                if(!uniqueStatements.contains(statement))
                    uniqueStatements << statement;
            }
            source.insert(bracePos + 1, "\n    " + uniqueStatements.join("\n    "));
        }

        notes << "Initialized GLSL out/inout parameters for BO3/FXC definite-assignment compatibility.";
        return source;
    }



    QString normalizeHlslFinalDefaultBreak(QString source, QStringList& notes) const
    {
        // FXC requires an explicit terminator for a non-empty switch arm. GLSL
        // permits the final default arm to reach the switch's closing brace.
        const QRegularExpression switchRe("\\bswitch\\s*\\([^)]*\\)\\s*\\{");
        QVector<int> insertions;

        int from = 0;
        while(from < source.size())
        {
            const auto sm = switchRe.match(source, from);
            if(!sm.hasMatch()) break;
            const int open = source.indexOf('{', sm.capturedStart());
            if(open < 0) break;
            const int close = glslFindMatchingForward(source, open);
            if(close < 0) break;

            const QString body = source.mid(open + 1, close - open - 1);
            const int defaultPos = body.lastIndexOf(QRegularExpression("\\bdefault\\s*:"));
            if(defaultPos >= 0)
            {
                const int colon = body.indexOf(':', defaultPos);
                if(colon >= 0)
                {
                    const QString tail = body.mid(colon + 1).trimmed();
                    if(!tail.isEmpty() &&
                       !QRegularExpression("\\b(?:break|return|discard)\\s*;\\s*$")
                            .match(tail).hasMatch())
                        insertions.push_back(close);
                }
            }
            from = close + 1;
        }

        for(int i = insertions.size() - 1; i >= 0; --i)
            source.insert(insertions[i], "\n        break;\n");

        if(!insertions.isEmpty())
            notes << "Added an explicit break to final non-empty default switch arms for legacy FXC.";
        return source;
    }

    QString normalizeGlslStpqSwizzles(QString source, const QSet<QString>& vectorNames, QStringList& notes) const
    {
        // GLSL vectors support three equivalent swizzle alphabets: xyzw, rgba,
        // and stpq. HLSL/FXC accepts xyzw/rgba but not stpq. A global name set is
        // insufficient because compact shaders routinely reuse one identifier as a
        // vector in one function and a custom struct in another. Resolve the nearest
        // visible concrete declaration first; only fall back to the legacy set when
        // no scoped type can be established.
        if(vectorNames.isEmpty())
            return source;

        QString scan = source;
        int lineStart = 0;
        bool continuation = false;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();
            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            const bool directive = continuation || (first < lineEnd && scan[first] == '#');
            if(directive)
            {
                for(int i = lineStart; i < lineEnd; ++i) scan[i] = ' ';
            }
            int tail = lineEnd - 1;
            while(tail >= lineStart && (source[tail] == ' ' || source[tail] == '\t')) --tail;
            continuation = directive && tail >= lineStart && source[tail] == '\\';
            lineStart = lineEnd < scan.size() ? lineEnd + 1 : scan.size();
        }

        QSet<QString> customTypes;
        const QRegularExpression structRe("\\bstruct\\s+([A-Za-z_]\\w*)\\s*\\{");
        auto structIt = structRe.globalMatch(scan);
        while(structIt.hasNext()) customTypes.insert(structIt.next().captured(1));

        struct ScopeRange
        {
            int open = -1;
            int close = -1;
            int depth = 0;
        };
        QVector<ScopeRange> scopes;
        QVector<int> scopeStack;
        for(int i = 0; i < scan.size(); ++i)
        {
            if(scan[i] == '{')
            {
                const int scopeIndex = scopes.size();
                scopes.push_back({i, static_cast<int>(scan.size()),
                                  static_cast<int>(scopeStack.size()) + 1});
                scopeStack.push_back(scopeIndex);
            }
            else if(scan[i] == '}' && !scopeStack.isEmpty())
            {
                const int scopeIndex = scopeStack.takeLast();
                scopes[scopeIndex].close = i;
            }
        }

        auto scopeAt = [&](int position) -> int
        {
            int best = -1;
            int bestDepth = 0;
            for(int i = 0; i < scopes.size(); ++i)
            {
                if(scopes[i].open < position && position < scopes[i].close &&
                   scopes[i].depth > bestDepth)
                {
                    best = i;
                    bestDepth = scopes[i].depth;
                }
            }
            return best;
        };

        struct ScopedBinding
        {
            QString name;
            QString type;
            int declaration = -1;
            int scope = -1;
        };
        QVector<ScopedBinding> bindings;

        QStringList concreteTypes = {
            "float", "int", "uint", "bool", "vec1",
            "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
            "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
            "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
            "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4"
        };
        for(const QString& customType : customTypes) concreteTypes << customType;
        std::sort(concreteTypes.begin(), concreteTypes.end(),
                  [](const QString& a, const QString& b) { return a.size() > b.size(); });
        QStringList escapedTypes;
        for(const QString& type : concreteTypes)
            escapedTypes << QRegularExpression::escape(type);
        const QString typePattern = QString("(?:%1)").arg(escapedTypes.join('|'));

        auto splitTopLevel = [](const QString& text) -> QStringList
        {
            QStringList parts;
            int paren = 0, bracket = 0, brace = 0, start = 0;
            for(int i = 0; i < text.size(); ++i)
            {
                const QChar c = text[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                {
                    parts << text.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            parts << text.mid(start).trimmed();
            return parts;
        };

        const QRegularExpression declarationRe(
            QString("\\b(?:(?:const|static|in|out|inout|lowp|mediump|highp)\\s+)*"
                    "(%1)\\s+([^;{}]+);").arg(typePattern));
        auto declarationIt = declarationRe.globalMatch(scan);
        while(declarationIt.hasNext())
        {
            const auto declaration = declarationIt.next();
            const QString type = declaration.captured(1);
            for(QString declarator : splitTopLevel(declaration.captured(2)))
            {
                declarator = declarator.section('=', 0, 0).trimmed();
                const auto nameMatch = QRegularExpression("^([A-Za-z_]\\w*)").match(declarator);
                if(!nameMatch.hasMatch()) continue;
                bindings.push_back({nameMatch.captured(1), type,
                                    static_cast<int>(declaration.capturedStart(2)),
                                    scopeAt(declaration.capturedStart(2))});
            }
        }

        // Function parameters are declared before their body's opening brace, but
        // their visibility belongs to that body scope. Record them explicitly.
        const QRegularExpression functionRe(
            "\\b[A-Za-z_]\\w*\\s+[A-Za-z_]\\w*\\s*\\(([^;{}]*)\\)\\s*\\{");
        const QRegularExpression parameterRe(
            QString("^(?:(?:const|in|out|inout|lowp|mediump|highp)\\s+)*"
                    "(%1)\\s+([A-Za-z_]\\w*)(?:\\s*\\[[^\\]]*\\])?$").arg(typePattern));
        auto functionIt = functionRe.globalMatch(scan);
        while(functionIt.hasNext())
        {
            const auto function = functionIt.next();
            const int bodyOpen = function.capturedEnd(0) - 1;
            const int bodyScope = scopeAt(bodyOpen + 1);
            for(const QString& parameterText : splitTopLevel(function.captured(1)))
            {
                const auto parameter = parameterRe.match(parameterText.trimmed());
                if(!parameter.hasMatch()) continue;
                bindings.push_back({parameter.captured(2), parameter.captured(1),
                                    static_cast<int>(function.capturedStart(1)), bodyScope});
            }
        }

        auto nearestType = [&](const QString& name, int usePosition) -> QString
        {
            QString type;
            int bestDepth = -1;
            int bestDeclaration = -1;
            for(const ScopedBinding& binding : bindings)
            {
                if(binding.name != name || binding.declaration > usePosition) continue;
                int depth = 0;
                if(binding.scope >= 0)
                {
                    const ScopeRange& scope = scopes[binding.scope];
                    if(!(scope.open < usePosition && usePosition < scope.close)) continue;
                    depth = scope.depth;
                }
                if(depth > bestDepth || (depth == bestDepth && binding.declaration > bestDeclaration))
                {
                    type = binding.type;
                    bestDepth = depth;
                    bestDeclaration = binding.declaration;
                }
            }
            return type;
        };

        const QRegularExpression swizzleRe(
            "\\b([A-Za-z_]\\w*)\\s*\\.\\s*([stpq]{1,4})\\b");
        auto it = swizzleRe.globalMatch(source);

        struct Edit { int start = -1; int length = 0; QString replacement; };
        QVector<Edit> edits;
        int converted = 0;
        while(it.hasNext())
        {
            const auto m = it.next();
            const QString base = m.captured(1);
            const QString scopedType = nearestType(base, m.capturedStart(1));
            const bool scopedVector = QRegularExpression("^[iub]?vec[234]$").match(scopedType).hasMatch();
            if((!scopedType.isEmpty() && !scopedVector) ||
               (scopedType.isEmpty() && !vectorNames.contains(base)))
                continue;

            QString swizzle = m.captured(2);
            for(int i = 0; i < swizzle.size(); ++i)
            {
                if(swizzle[i] == 's') swizzle[i] = 'x';
                else if(swizzle[i] == 't') swizzle[i] = 'y';
                else if(swizzle[i] == 'p') swizzle[i] = 'z';
                else if(swizzle[i] == 'q') swizzle[i] = 'w';
            }
            edits.push_back({static_cast<int>(m.capturedStart(2)),
                             static_cast<int>(m.capturedLength(2)),
                             swizzle});
            ++converted;
        }

        for(int i = edits.size() - 1; i >= 0; --i)
            source.replace(edits[i].start, edits[i].length, edits[i].replacement);

        if(converted > 0)
            notes << QString("Normalized %1 GLSL stpq vector swizzle(s) to FXC-compatible xyzw aliases using nearest scoped concrete types.").arg(converted);
        return source;
    }

    QString initializeHlslUninitializedLocals(QString source, QStringList& notes) const
    {
        // GLSL leaves uninitialized locals undefined. FXC is stricter and rejects
        // many of those reads. Give simple locals a deterministic zero value.
        // Also handle comma declaration lists such as:
        //     vec3 d = vec3(...), p, o;
        // where p/o are independent uninitialized locals in GLSL.
        QString scan = source;
        int lineStart = 0;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();
            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            if(first < lineEnd && scan[first] == '#')
                for(int i = lineStart; i < lineEnd; ++i) scan[i] = ' ';
            lineStart = (lineEnd < scan.size()) ? lineEnd + 1 : scan.size();
        }

        QSet<QString> knownCustomTypes;
        const QRegularExpression structTypeRe("\\bstruct\\s+([A-Za-z_]\\w*)\\s*\\{");
        auto stit = structTypeRe.globalMatch(source);
        while(stit.hasNext())
            knownCustomTypes.insert(stit.next().captured(1));

        // Preserve simple object-like type aliases such as `#define vec1 float`.
        const QRegularExpression typeAliasRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s+"
            "((?:float|int|uint|bool)(?:[234](?:x[234])?)?)\\s*$",
            QRegularExpression::MultilineOption);
        auto tait = typeAliasRe.globalMatch(source);
        while(tait.hasNext())
            knownCustomTypes.insert(tait.next().captured(1));

        const QRegularExpression numericTypeRe(
            "^(?:float|int|uint|bool)(?:[234](?:x[234])?)?$");

        const QRegularExpression functionRe(
            "(?:^|[;}])\\s*((?:float|int|uint|bool)(?:[234](?:x[234])?)?|[A-Za-z_]\\w*)\\s+"
            "([A-Za-z_]\\w*)\\s*\\([^;{}]*\\)\\s*\\{",
            QRegularExpression::MultilineOption);

        QStringList localTypePatterns;
        localTypePatterns << "(?:float|int|uint|bool)(?:[234](?:x[234])?)?";
        for(const QString& typeName : knownCustomTypes)
            localTypePatterns << QRegularExpression::escape(typeName);
        const QString localTypePattern = QString("(?:%1)").arg(localTypePatterns.join('|'));
        const QRegularExpression localStatementRe(
            QString("(^|[;{}])([ \\t\\r\\n]*)(%1)\\s+([^;]+);").arg(localTypePattern),
            QRegularExpression::MultilineOption);

        auto splitTopLevelCommas = [](const QString& text)
        {
            QStringList parts;
            int paren = 0, bracket = 0, brace = 0;
            int start = 0;
            for(int i = 0; i < text.size(); ++i)
            {
                const QChar c = text[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == ',' && paren == 0 && bracket == 0 && brace == 0)
                {
                    parts << text.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            parts << text.mid(start).trimmed();
            return parts;
        };

        auto hasTopLevelInitializer = [](const QString& text)
        {
            int paren = 0, bracket = 0, brace = 0;
            for(int i = 0; i < text.size(); ++i)
            {
                const QChar c = text[i];
                if(c == '(') ++paren;
                else if(c == ')') --paren;
                else if(c == '[') ++bracket;
                else if(c == ']') --bracket;
                else if(c == '{') ++brace;
                else if(c == '}') --brace;
                else if(c == '=' && paren == 0 && bracket == 0 && brace == 0)
                {
                    const QChar prev = i > 0 ? text[i - 1] : QChar();
                    const QChar next = i + 1 < text.size() ? text[i + 1] : QChar();
                    if(prev != '=' && prev != '!' && prev != '<' && prev != '>' &&
                       prev != '+' && prev != '-' && prev != '*' && prev != '/' &&
                       prev != '%' && prev != '&' && prev != '|' && prev != '^' &&
                       next != '=')
                        return true;
                }
            }
            return false;
        };

        struct Edit { int start = -1; int length = 0; QString replacement; int initializedCount = 0; };
        QVector<Edit> edits;

        int from = 0;
        while(from < scan.size())
        {
            const auto fm = functionRe.match(scan, from);
            if(!fm.hasMatch()) break;
            const int open = scan.indexOf('{', fm.capturedStart());
            if(open < 0) break;
            const int close = glslFindMatchingForward(scan, open);
            if(close < 0) break;

            const QString body = source.mid(open + 1, close - open - 1);
            auto lit = localStatementRe.globalMatch(body);
            while(lit.hasNext())
            {
                const auto lm = lit.next();
                const QString typeName = lm.captured(3);
                if(!numericTypeRe.match(typeName).hasMatch() &&
                   !knownCustomTypes.contains(typeName))
                    continue;

                QStringList declarators = splitTopLevelCommas(lm.captured(4));
                if(declarators.isEmpty())
                    continue;

                int changedCount = 0;
                for(QString& declarator : declarators)
                {
                    const QString trimmed = declarator.trimmed();
                    if(trimmed.isEmpty() || hasTopLevelInitializer(trimmed))
                        continue;

                    // `(type)0` is safe for scalar/vector/matrix/custom values, but
                    // not for an array declarator. Leave arrays to dedicated lowering.
                    if(trimmed.contains('['))
                        continue;

                    if(!QRegularExpression("^[A-Za-z_]\\w*$").match(trimmed).hasMatch())
                        continue;

                    declarator = QString("%1 = (%2)0").arg(trimmed, typeName);
                    ++changedCount;
                }

                if(changedCount == 0)
                    continue;

                const int absStart = open + 1 + lm.capturedStart(3);
                const int absEnd = open + 1 + lm.capturedEnd();
                edits.push_back({
                    absStart,
                    absEnd - absStart,
                    QString("%1 %2;").arg(typeName, declarators.join(", ")),
                    changedCount
                });
            }

            from = close + 1;
        }

        if(edits.isEmpty())
            return source;

        std::sort(edits.begin(), edits.end(),
                  [](const Edit& a, const Edit& b) { return a.start > b.start; });
        int lastStart = static_cast<int>(source.size()) + 1;
        int appliedVariables = 0;
        for(const Edit& edit : edits)
        {
            if(edit.start < 0 || edit.start + edit.length > source.size())
                continue;
            if(edit.start >= lastStart)
                continue;
            source.replace(edit.start, edit.length, edit.replacement);
            lastStart = edit.start;
            appliedVariables += edit.initializedCount;
        }

        if(appliedVariables > 0)
            notes << QString("Zero-initialized %1 uninitialized local variable(s), including comma declaration lists, for deterministic FXC compilation.").arg(appliedVariables);
        return source;
    }

    QString appendHlslFallbackReturns(QString source, QStringList& notes) const
    {
        // FXC is stricter than many GLSL drivers about proving that every path
        // in a non-void function returns a value. Add a deterministic zero
        // fallback when the final top-level statement is not an unconditional
        // return. This also covers GLSL such as:
        //     float f(float x) { if (x > 0.0) return x; }
        // where the trailing return is conditional and FXC correctly reports
        // that another control-flow path has no value.
        //
        // Do all function discovery against a position-preserving copy with
        // preprocessor lines masked. Without this, a macro body containing a
        // constructor/call can be mistaken for a function signature and the
        // scanner can run all the way into the next real function body.
        QString scan = source;
        int lineStart = 0;
        while(lineStart < scan.size())
        {
            int lineEnd = scan.indexOf('\n', lineStart);
            if(lineEnd < 0) lineEnd = scan.size();

            int first = lineStart;
            while(first < lineEnd && (scan[first] == ' ' || scan[first] == '\t')) ++first;
            if(first < lineEnd && scan[first] == '#')
            {
                for(int i = lineStart; i < lineEnd; ++i)
                    scan[i] = ' ';
            }

            lineStart = (lineEnd < scan.size()) ? lineEnd + 1 : scan.size();
        }

        const QRegularExpression functionRe(
            "(?:^|[;}])\\s*((?:float|int|uint|bool)(?:[234](?:x[234])?)?|[A-Za-z_]\\w*)\\s+([A-Za-z_]\\w*)\\s*\\([^;{}]*\\)\\s*\\{",
            QRegularExpression::MultilineOption);

        struct Insertion { int pos; QString text; };
        QVector<Insertion> insertions;

        int from = 0;
        while(from < scan.size())
        {
            const auto m = functionRe.match(scan, from);
            if(!m.hasMatch()) break;

            const QString returnType = m.captured(1);
            const int open = scan.indexOf('{', m.capturedStart());
            if(open < 0) break;
            const int close = glslFindMatchingForward(scan, open);
            if(close < 0) break;

            if(returnType == "void")
            {
                from = close + 1;
                continue;
            }

            // Only suppress the fallback when the function literally ends in a
            // top-level `return ...;`. A conditional `if (...) return ...;` must
            // still receive the fallback because the false branch has no value.
            bool endsWithDirectReturn = false;
            int tail = glslSkipSpacesBackward(scan, close - 1);
            if(tail > open && scan[tail] == ';')
            {
                const int statementEnd = tail;
                int parenDepth = 0;
                int bracketDepth = 0;
                int braceDepth = 0;
                int statementStart = open + 1;

                for(int i = tail - 1; i > open; --i)
                {
                    const QChar c = scan[i];
                    if(c == ')') ++parenDepth;
                    else if(c == '(' && parenDepth > 0) --parenDepth;
                    else if(c == ']') ++bracketDepth;
                    else if(c == '[' && bracketDepth > 0) --bracketDepth;
                    else if(c == '}')
                    {
                        if(parenDepth == 0 && bracketDepth == 0 && braceDepth == 0)
                        {
                            statementStart = i + 1;
                            break;
                        }
                        ++braceDepth;
                    }
                    else if(c == '{')
                    {
                        if(braceDepth > 0) --braceDepth;
                        else if(parenDepth == 0 && bracketDepth == 0)
                        {
                            statementStart = i + 1;
                            break;
                        }
                    }
                    else if(parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && c == ';')
                    {
                        statementStart = i + 1;
                        break;
                    }
                }

                const QString finalStatement = source.mid(
                    statementStart, statementEnd - statementStart + 1).trimmed();
                endsWithDirectReturn = QRegularExpression(
                    "^return\\b[^;{}]*;\\s*$",
                    QRegularExpression::DotMatchesEverythingOption)
                    .match(finalStatement).hasMatch();
            }

            if(!endsWithDirectReturn)
            {
                insertions.push_back({
                    close,
                    QString("\n    return (%1)0;\n").arg(returnType)
                });
            }

            from = close + 1;
        }

        for(int i = insertions.size() - 1; i >= 0; --i)
            source.insert(insertions[i].pos, insertions[i].text);

        if(!insertions.isEmpty())
            notes << "Added FXC-safe fallback returns to non-void GLSL functions whose final return path could not be proven.";

        return source;
    }

    QString lowerRuntimeDependentGlslGlobals(QString source, QStringList& notes) const
    {
        QStringList lines = source.split('\n');
        QStringList initializers;
        int braceDepth = 0;
        const QRegularExpression declRe(
            R"(^\s*static\s+((?:float|int|uint|bool)(?:[234](?:x[234])?)?)\s+([A-Za-z_]\w*)\s*=\s*(.+);\s*$)");
        const QRegularExpression runtimeRe(
            R"(\b(?:iTime|iTimeDelta|iFrame|iFrameRate|iResolution|iMouse|iDate|GLSL_FRAGCOORD)\b)");

        for(QString& line : lines)
        {
            if(braceDepth == 0)
            {
                const auto m = declRe.match(line);
                if(m.hasMatch() && m.captured(3).contains(runtimeRe))
                {
                    const QString type = m.captured(1);
                    const QString name = m.captured(2);
                    const QString expr = m.captured(3).trimmed();
                    line = QString("static %1 %2 = (%1)0;").arg(type, name);
                    initializers << QString("    %1 = %2;").arg(name, expr);
                }
            }
            braceDepth += line.count('{') - line.count('}');
        }
        if(initializers.isEmpty()) return source;

        source = lines.join("\n");
        QRegularExpression entryRe(R"(\bvoid\s+(mainImage|main)\s*\([^\)]*\)\s*\{)");
        const auto entry = entryRe.match(source);
        if(!entry.hasMatch()) return source;
        source.insert(entry.capturedEnd(), "\n    // Runtime-dependent GLSL global initialization.\n" + initializers.join("\n") + "\n");
        notes << QString("Lowered %1 runtime-dependent GLSL global initializer(s) into the fragment entry point for FXC compatibility.")
                     .arg(initializers.size());
        return source;
    }

    QString normalizeConstantLoopsForFxc(QString source, QStringList& notes) const
    {
        QRegularExpression loopRe(
            R"((for\s*\(\s*int\s+([A-Za-z_]\w*)\s*=\s*(-?\d+)\s*;\s*\2\s*(<|<=)\s*(-?\d+)\s*;\s*(?:\+\+\2|\2\+\+|\2\s*\+=\s*1)\s*\)))");
        int changed = 0;
        int offset = 0;
        while(true)
        {
            const auto m = loopRe.match(source, offset);
            if(!m.hasMatch()) break;
            const int at = m.capturedStart(1);
            const QString prefix = source.mid(qMax(0, at - 16), qMin(16, at));
            if(prefix.contains("[unroll]"))
            {
                offset = m.capturedEnd(1);
                continue;
            }
            const int start = m.captured(3).toInt();
            const int end = m.captured(5).toInt();
            const bool inclusive = m.captured(4) == "<=";
            const int iterations = end - start + (inclusive ? 1 : 0);
            if(iterations > 0 && iterations <= 64)
            {
                source.insert(at, "[unroll] ");
                offset = m.capturedEnd(1) + 9;
                ++changed;
            }
            else offset = m.capturedEnd(1);
        }
        if(changed > 0)
            notes << QString("Added FXC [unroll] hints to %1 small constant integer loop(s).").arg(changed);
        return source;
    }

QString convertGlslSyntax(QString source, QStringList& notes, QSet<int>& usedChannels, QStringList& varyingAliases) const
    {
        source.replace("\r\n", "\n");
        source.replace('\r', '\n');
        source = stripGlslComments(source, notes);
        source = normalizeGlslEmptyFunctionMacros(source, notes);
        source = normalizeGlslEmptyMacroArguments(source, notes);
        source = normalizeTwiglCompatibilityAliases(source, notes);
        source = preExpandGlslFragmentObjectMacros(source, notes);
        source = preExpandGlslFxcSensitiveMacros(source, notes);
        source = inlineGlslSwappedOverloadForwarders(source, notes);
        source = inlineGlslExactStructOverloadCalls(source, notes);
        source = normalizeGlslRelaxedScalarConstructors(source, notes);
        source = normalizeGlslDynamicVectorWrites(source, notes);
        source = renameGlslBo3HeaderCollisions(source, notes);
        source = promoteGlslGlobalConstants(source, notes);
        source = compactConvertedGlslWhitespace(source);
        source = expandGlslShadertoyBuiltinAliasMacros(source, notes);
        source = renameGlslShadowedBuiltins(source, notes);
        source = renameGlslHlslIntrinsicCollisions(source, notes);
        source = renameGlslHlslReservedIdentifiers(source, notes);
        source = renameGlslFunctionShadowingLocals(source, notes);
        source = renameGlslTypeShadowingLocals(source, notes);
        source = normalizeGlslPrefixArrayDeclarators(source, notes);
        source = lowerGlslGolfedCommaTernaries(source, notes);

        // Capture GLSL scalar/vector/matrix identifiers before their type tokens
        // are translated. Matrix tracking includes comma-separated declarations,
        // matrix-returning functions, and #define helpers such as:
        //     #define rot(a) mat2(...)
        QSet<QString> matrixNames;
        QRegularExpression matrixDeclRe("\\bmat(?:[234](?:x[234])?)\\s+([A-Za-z_]\\w*)");
        auto mit = matrixDeclRe.globalMatch(source);
        while(mit.hasNext()) matrixNames.insert(mit.next().captured(1));

        QRegularExpression matrixStatementRe(
            "\\bmat(?:[234](?:x[234])?)\\s+([^;\\n\\{]+);");
        auto msit = matrixStatementRe.globalMatch(source);
        while(msit.hasNext())
        {
            const QString namesPart = msit.next().captured(1);
            for(QString part : namesPart.split(',', Qt::SkipEmptyParts))
            {
                part = part.trimmed();
                if(part.contains('(')) continue; // prototype / function
                part = part.section('=', 0, 0).trimmed();
                part = part.section('[', 0, 0).trimmed();
                const auto nm = QRegularExpression("^([A-Za-z_]\\w*)$").match(part);
                if(nm.hasMatch()) matrixNames.insert(nm.captured(1));
            }
        }

        QRegularExpression matrixMacroRe(
            "^\\s*#define\\s+([A-Za-z_]\\w*)\\s*\\([^\\n]*\\)\\s+[^\\n]*\\bmat(?:[234](?:x[234])?)\\b",
            QRegularExpression::MultilineOption);
        auto mmit = matrixMacroRe.globalMatch(source);
        while(mmit.hasNext()) matrixNames.insert(mmit.next().captured(1));

        QSet<QString> vectorNames;
        QRegularExpression vectorDeclRe("\\b(?:vec[234]|ivec[234]|uvec[234]|bvec[234])\\s+([A-Za-z_]\\w*)");
        auto veit = vectorDeclRe.globalMatch(source);
        while(veit.hasNext()) vectorNames.insert(veit.next().captured(1));

        QSet<QString> scalarNames;
        QRegularExpression scalarDeclRe("\\b(?:float|int|uint|bool)\\s+([A-Za-z_]\\w*)");
        auto seit = scalarDeclRe.globalMatch(source);
        while(seit.hasNext()) scalarNames.insert(seit.next().captured(1));

        source = normalizeGlslStpqSwizzles(source, vectorNames, notes);

        // BO3's common shader headers already define M_PI. Guard Shadertoy copies.
        QRegularExpression mpiRe("^\\s*#define\\s+M_PI\\s+([^\\n]+)$", QRegularExpression::MultilineOption);
        if(mpiRe.match(source).hasMatch())
        {
            source.replace(mpiRe, "#ifndef M_PI\n#define M_PI \\1\n#endif");
            notes << "Guarded M_PI because BO3 shader headers may already define it.";
        }

        for(int i=0;i<4;++i)
        {
            if(QRegularExpression(QString("\\biChannel%1\\b").arg(i)).match(source).hasMatch())
                usedChannels.insert(i);
        }
        if(!usedChannels.isEmpty())
        {
            QStringList names;
            for(int channel : usedChannels) names << QString("iChannel%1").arg(channel);
            notes << QString("Detected Shadertoy texture inputs: %1. Load the matching images in Material Textures > Shadertoy / GLSL Texture Inputs.").arg(names.join(", "));
        }

        // Capture common fragment varyings before removing their GLSL declarations.
        QRegularExpression varyingRe("^\\s*(?:layout\\s*\\([^\\n]*\\)\\s*)?(?:flat\\s+)?in\\s+vec2\\s+([A-Za-z_]\\w*)\\s*;\\s*$",
                                     QRegularExpression::MultilineOption);
        auto vit = varyingRe.globalMatch(source);
        while(vit.hasNext()) varyingAliases << vit.next().captured(1);
        source.remove(varyingRe);

        source.remove(QRegularExpression("^\\s*#version[^\\n]*\\n?", QRegularExpression::MultilineOption));
        source.remove(QRegularExpression("^\\s*precision\\s+(?:lowp|mediump|highp)\\s+\\w+\\s*;\\s*$", QRegularExpression::MultilineOption));
        source.replace(QRegularExpression("\\blayout\\s*\\([^\\)]*\\)\\s*"), "");
        source.replace(QRegularExpression("\\b(?:lowp|mediump|highp|flat|smooth|centroid|noperspective)\\s+"), "");
        QRegularExpression fragOutRe("^\\s*(?:layout\\s*\\([^\\n]*\\)\\s*)?out\\s+vec4\\s+([A-Za-z_]\\w*)\\s*;\\s*$",
                                     QRegularExpression::MultilineOption);
        QStringList fragmentOutputs;
        auto fit = fragOutRe.globalMatch(source);
        while(fit.hasNext()) fragmentOutputs << fit.next().captured(1);
        source.remove(fragOutRe);
        for(const QString& outputName : fragmentOutputs)
        {
            if(outputName != "fragColor")
                source.replace(QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(outputName))), "fragColor");
        }
        // Shadertoy declarations may be minified onto the same line as functions.
        // Remove these known wrapper-provided uniforms wherever they occur instead
        // of requiring the declaration to occupy an entire physical line.
        source.remove(QRegularExpression("\\buniform\\s+(?:float|vec2|vec3|vec4|int)\\s+(?:iTime|iTimeDelta|iFrame|iFrameRate|iResolution|iMouse|iDate)\\s*;"));
        source.remove(QRegularExpression("\\buniform\\s+sampler(?:2D|Cube)\\s+iChannel[0-3]\\s*;"));

        // Shadertoy exposes per-channel dimensions through iChannelResolution[].
        // BO3 preview channels use the preview target dimensions by default.
        if(source.contains(QRegularExpression("\\biChannelResolution\\s*\\[")))
        {
            source.replace(QRegularExpression("\\biChannelResolution\\s*\\[\\s*([^\\]\\n]+)\\s*\\]"),
                           "GLSL_CHANNEL_RESOLUTION(\\1)");
            notes << "Mapped Shadertoy iChannelResolution[] to the BO3 preview target size.";
        }


        // Generic user samplers remain resources; all of them share the converter's
        // preview sampler unless the user later assigns a dedicated sampler manually.
        source.replace(QRegularExpression("\\buniform\\s+sampler2D\\s+([A-Za-z_]\\w*)\\s*;"), "Texture2D<float4> \\1;");
        source.replace(QRegularExpression("\\buniform\\s+samplerCube\\s+([A-Za-z_]\\w*)\\s*;"), "TextureCube<float4> \\1;");

        // GLSL top-level variables are shader-private and can be mutable. Do this
        // while custom `uniform` qualifiers are still present so real external
        // shader inputs are not accidentally internalized.
        source = promoteGlslMutableGlobals(source, notes);
        source.replace(QRegularExpression("\\buniform\\s+"), "");

        // Combined GLSL sampler types may also occur as function parameters.
        // Convert any residual combined sampler token into an HLSL resource type.
        source.replace(QRegularExpression("\\bsampler2D\\b"), "Texture2D<float4>");
        source.replace(QRegularExpression("\\bsamplerCube\\b"), "TextureCube<float4>");

        // Preserve GLSL array/struct/matrix/vector constructor semantics before
        // their type tokens are renamed to HLSL.
        source = convertGlslArrayConstructors(source, notes);
        source = convertGlslStructConstructors(source, notes);
        source = convertGlslSingleArgumentMatrixConstructors(source, notes);
        source = convertGlslSingleArgumentVectorConstructors(source, notes);

        const QVector<QPair<QString,QString>> tokens = {
            {"vec2","float2"},{"vec3","float3"},{"vec4","float4"},
            {"ivec2","int2"},{"ivec3","int3"},{"ivec4","int4"},
            {"uvec2","uint2"},{"uvec3","uint3"},{"uvec4","uint4"},
            {"bvec2","bool2"},{"bvec3","bool3"},{"bvec4","bool4"},
            {"mat2x2","float2x2"},{"mat2x3","float2x3"},{"mat2x4","float2x4"},
            {"mat3x2","float3x2"},{"mat3x3","float3x3"},{"mat3x4","float3x4"},
            {"mat4x2","float4x2"},{"mat4x3","float4x3"},{"mat4x4","float4x4"},
            {"mat2","float2x2"},{"mat3","float3x3"},{"mat4","float4x4"},
            {"mix","lerp"},{"fract","frac"},{"inversesqrt","rsqrt"},{"roundEven","round"},
            {"dFdx","ddx"},{"dFdy","ddy"},{"floatBitsToUint","asuint"},
            {"floatBitsToInt","asint"},{"uintBitsToFloat","asfloat"},{"intBitsToFloat","asfloat"}
        };
        for(const auto& pair : tokens)
            source.replace(QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(pair.first))), pair.second);

        const bool usesFragCoord = source.contains(QRegularExpression("\\bgl_FragCoord\\b"));
        source.replace(QRegularExpression("\\bgl_FragCoord\\b"), "GLSL_FRAGCOORD");
        if(usesFragCoord)
            notes << "Mapped GLSL gl_FragCoord to a shader-private fragment-coordinate value so helper functions can access it.";
        source.replace(QRegularExpression("\\bgl_FragColor\\b"), "fragColor");
        source.replace(QRegularExpression("\\bmod\\s*\\("), "GLSL_MOD(");
        source = convertGlslFunctionCalls(source);
        source = lowerRuntimeDependentGlslGlobals(source, notes);
        source = normalizeConstantLoopsForFxc(source, notes);
        source = routeGlslChannelSamplers(source, usedChannels, notes);
        source = stabilizeHlslLongLoopTextureSampling(source, notes);
        source = convertGlslVectorEquality(source, vectorNames, notes);
        source = convertGlslMatrixMultiplication(source, matrixNames, vectorNames, scalarNames, notes);
        source = normalizeHlslFinalDefaultBreak(source, notes);
        source = initializeHlslUninitializedLocals(source, notes);
        source = initializeHlslOutParameters(source, notes);
        source = appendHlslFallbackReturns(source, notes);

        // A classic GLSL fragment main() is treated like a Shadertoy mainImage.
        bool convertedClassicMain = false;
        if(!QRegularExpression("\\bmainImage\\s*\\(").match(source).hasMatch())
        {
            QRegularExpression mainRe("\\bvoid\\s+main\\s*\\(\\s*\\)");
            if(mainRe.match(source).hasMatch())
            {
                source.replace(mainRe, "void mainImage(out float4 fragColor, in float2 fragCoord)");
                notes << "Converted GLSL main() into a Shadertoy-style mainImage() entry.";
                convertedClassicMain = true;
            }
        }
        if(convertedClassicMain)
            source = initializeHlslOutParameters(source, notes);

        if(!QRegularExpression("\\bmainImage\\s*\\(").match(source).hasMatch())
            notes << "No mainImage() or fragment main() was found. The syntax was translated, but you may need to connect your own entry function.";
        if(source.contains("sampler", Qt::CaseInsensitive))
            notes << "Sampler declarations were translated heuristically; verify custom texture bindings before BO3 export.";
        if(source.contains(QRegularExpression("\bfloat[234]x[234]\b")))
            notes << "Matrix conversion is handled heuristically. Verify complex matrix expressions or externally supplied matrices in the preview.";
        return source.trimmed() + "\n";
    }

    QString glslCompatibilityHelpers(const QString& converted = QString(), bool emitFullLibrary = false) const
    {
        const QString allHelpers = QStringLiteral(R"GLSLHLSL(
// GLSL gl_FragCoord is a fragment-stage built-in visible from helper functions.
// ps_main updates this once per pixel before mainImage() runs.
static float4 GLSL_FRAGCOORD = float4(0.0, 0.0, 0.0, 1.0);

// GLSL square-matrix one-argument constructors need explicit compatibility.
// These helpers preserve diagonal scalar construction plus identity extension /
// upper-left truncation when converting between mat2/mat3/mat4.
float2x2 GLSL_MAT2(float s) { return float2x2(s, 0.0, 0.0, s); }
float2x2 GLSL_MAT2(float2x2 m) { return m; }
float2x2 GLSL_MAT2(float3x3 m) { return float2x2(m[0][0], m[0][1], m[1][0], m[1][1]); }
float2x2 GLSL_MAT2(float4x4 m) { return float2x2(m[0][0], m[0][1], m[1][0], m[1][1]); }

float3x3 GLSL_MAT3(float s) { return float3x3(s,0.0,0.0, 0.0,s,0.0, 0.0,0.0,s); }
float3x3 GLSL_MAT3(float2x2 m) { return float3x3(m[0][0],m[0][1],0.0, m[1][0],m[1][1],0.0, 0.0,0.0,1.0); }
float3x3 GLSL_MAT3(float3x3 m) { return m; }
float3x3 GLSL_MAT3(float4x4 m) { return float3x3(m[0][0],m[0][1],m[0][2], m[1][0],m[1][1],m[1][2], m[2][0],m[2][1],m[2][2]); }

float4x4 GLSL_MAT4(float s) { return float4x4(s,0.0,0.0,0.0, 0.0,s,0.0,0.0, 0.0,0.0,s,0.0, 0.0,0.0,0.0,s); }
float4x4 GLSL_MAT4(float2x2 m) { return float4x4(m[0][0],m[0][1],0.0,0.0, m[1][0],m[1][1],0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0); }
float4x4 GLSL_MAT4(float3x3 m) { return float4x4(m[0][0],m[0][1],m[0][2],0.0, m[1][0],m[1][1],m[1][2],0.0, m[2][0],m[2][1],m[2][2],0.0, 0.0,0.0,0.0,1.0); }
float4x4 GLSL_MAT4(float4x4 m) { return m; }

float2x2 GLSL_INVERSE(float2x2 m)
{
    float det = m[0][0]*m[1][1] - m[0][1]*m[1][0];
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float2x2(
         m[1][1], -m[0][1],
        -m[1][0],  m[0][0]) / det;
}
float3x3 GLSL_INVERSE(float3x3 m)
{
    float a=m[0][0], b=m[0][1], c=m[0][2];
    float d=m[1][0], e=m[1][1], f=m[1][2];
    float g=m[2][0], h=m[2][1], i=m[2][2];
    float det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float3x3(
        e*i-f*h, c*h-b*i, b*f-c*e,
        f*g-d*i, a*i-c*g, c*d-a*f,
        d*h-e*g, b*g-a*h, a*e-b*d) / det;
}
float GLSL_DET3(
    float a00,float a01,float a02,
    float a10,float a11,float a12,
    float a20,float a21,float a22)
{
    return a00*(a11*a22-a12*a21)
         - a01*(a10*a22-a12*a20)
         + a02*(a10*a21-a11*a20);
}
float4x4 GLSL_INVERSE(float4x4 m)
{
    float c00 = GLSL_DET3(m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]);
    float c01 = -(GLSL_DET3(m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]));
    float c02 = GLSL_DET3(m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]);
    float c03 = -(GLSL_DET3(m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]));
    float c10 = -(GLSL_DET3(m[0][1], m[0][2], m[0][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]));
    float c11 = GLSL_DET3(m[0][0], m[0][2], m[0][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]);
    float c12 = -(GLSL_DET3(m[0][0], m[0][1], m[0][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]));
    float c13 = GLSL_DET3(m[0][0], m[0][1], m[0][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    float c20 = GLSL_DET3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[3][1], m[3][2], m[3][3]);
    float c21 = -(GLSL_DET3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[3][0], m[3][2], m[3][3]));
    float c22 = GLSL_DET3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[3][0], m[3][1], m[3][3]);
    float c23 = -(GLSL_DET3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[3][0], m[3][1], m[3][2]));
    float c30 = -(GLSL_DET3(m[0][1], m[0][2], m[0][3], m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3]));
    float c31 = GLSL_DET3(m[0][0], m[0][2], m[0][3], m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3]);
    float c32 = -(GLSL_DET3(m[0][0], m[0][1], m[0][3], m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3]));
    float c33 = GLSL_DET3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]);
    float det = m[0][0]*c00 + m[0][1]*c01 + m[0][2]*c02 + m[0][3]*c03;
    det = abs(det) < 1e-12 ? (det < 0.0 ? -1e-12 : 1e-12) : det;
    return float4x4(
        c00, c10, c20, c30,
        c01, c11, c21, c31,
        c02, c12, c22, c32,
        c03, c13, c23, c33) / det;
}

int2 GLSL_TEXTURE_SIZE(Texture2D<float4> tex, int lod)
{
    uint w=1, h=1, levels=1;
    tex.GetDimensions((uint)max(lod,0), w, h, levels);
    return int2(w,h);
}
int2 GLSL_TEXTURE_SIZE(TextureCube<float4> tex, int lod)
{
    uint w=1, h=1, levels=1;
    tex.GetDimensions((uint)max(lod,0), w, h, levels);
    return int2(w,h);
}


float3 GLSL_CHANNEL_RESOLUTION(int channel)
{
    return iResolution;
}

// GLSL permits vecN(scalar) splats and vector copy/truncation constructors.
// Do not overload these helpers: BO3's legacy FXC resolver can treat scalar/vector
// promotions as equally viable and report X3067 for calls such as vec3(0).
float2 GLSL_VEC2_S(float x)    { return float2(x, x); }
float2 GLSL_VEC2_V2(float2 x)  { return x; }
float2 GLSL_VEC2_V3(float3 x)  { return x.xy; }
float2 GLSL_VEC2_V4(float4 x)  { return x.xy; }
float3 GLSL_VEC3_S(float x)    { return float3(x, x, x); }
float3 GLSL_VEC3_V2(float2 x)  { return float3(x, 0.0); }
float3 GLSL_VEC3_V3(float3 x)  { return x; }
float3 GLSL_VEC3_V4(float4 x)  { return x.xyz; }
float4 GLSL_VEC4_S(float x)    { return float4(x, x, x, x); }
float4 GLSL_VEC4_V2(float2 x)  { return float4(x, 0.0, 0.0); }
float4 GLSL_VEC4_V3(float3 x)  { return float4(x, 0.0); }
float4 GLSL_VEC4_V4(float4 x)  { return x; }

int2 GLSL_IVEC2_S(int x)      { return int2(x, x); }
int2 GLSL_IVEC2_V2(int2 x)    { return x; }
int2 GLSL_IVEC2_V3(int3 x)    { return x.xy; }
int2 GLSL_IVEC2_V4(int4 x)    { return x.xy; }
int3 GLSL_IVEC3_S(int x)      { return int3(x, x, x); }
int3 GLSL_IVEC3_V2(int2 x)    { return int3(x, 0); }
int3 GLSL_IVEC3_V3(int3 x)    { return x; }
int3 GLSL_IVEC3_V4(int4 x)    { return x.xyz; }
int4 GLSL_IVEC4_S(int x)      { return int4(x, x, x, x); }
int4 GLSL_IVEC4_V2(int2 x)    { return int4(x, 0, 0); }
int4 GLSL_IVEC4_V3(int3 x)    { return int4(x, 0); }
int4 GLSL_IVEC4_V4(int4 x)    { return x; }

uint2 GLSL_UVEC2_S(uint x)    { return uint2(x, x); }
uint2 GLSL_UVEC2_V2(uint2 x)  { return x; }
uint2 GLSL_UVEC2_V3(uint3 x)  { return x.xy; }
uint2 GLSL_UVEC2_V4(uint4 x)  { return x.xy; }
uint3 GLSL_UVEC3_S(uint x)    { return uint3(x, x, x); }
uint3 GLSL_UVEC3_V2(uint2 x)  { return uint3(x, 0); }
uint3 GLSL_UVEC3_V3(uint3 x)  { return x; }
uint3 GLSL_UVEC3_V4(uint4 x)  { return x.xyz; }
uint4 GLSL_UVEC4_S(uint x)    { return uint4(x, x, x, x); }
uint4 GLSL_UVEC4_V2(uint2 x)  { return uint4(x, 0, 0); }
uint4 GLSL_UVEC4_V3(uint3 x)  { return uint4(x, 0); }
uint4 GLSL_UVEC4_V4(uint4 x)  { return x; }

bool2 GLSL_BVEC2_S(bool x)    { return bool2(x, x); }
bool2 GLSL_BVEC2_V2(bool2 x)  { return x; }
bool2 GLSL_BVEC2_V3(bool3 x)  { return x.xy; }
bool2 GLSL_BVEC2_V4(bool4 x)  { return x.xy; }
bool3 GLSL_BVEC3_S(bool x)    { return bool3(x, x, x); }
bool3 GLSL_BVEC3_V2(bool2 x)  { return bool3(x, false); }
bool3 GLSL_BVEC3_V3(bool3 x)  { return x; }
bool3 GLSL_BVEC3_V4(bool4 x)  { return x.xyz; }
bool4 GLSL_BVEC4_S(bool x)    { return bool4(x, x, x, x); }
bool4 GLSL_BVEC4_V2(bool2 x)  { return bool4(x, false, false); }
bool4 GLSL_BVEC4_V3(bool3 x)  { return bool4(x, false); }
bool4 GLSL_BVEC4_V4(bool4 x)  { return x; }

// GLSL component-wise relational builtins. HLSL comparison operators already
// return boolN for vector operands, so macros preserve the GLSL result width
// without legacy-FXC overload ambiguity.
#define GLSL_LESS_THAN(x, y) ((x) < (y))
#define GLSL_LESS_THAN_EQUAL(x, y) ((x) <= (y))
#define GLSL_GREATER_THAN(x, y) ((x) > (y))
#define GLSL_GREATER_THAN_EQUAL(x, y) ((x) >= (y))
#define GLSL_EQUAL(x, y) ((x) == (y))
#define GLSL_NOT_EQUAL(x, y) ((x) != (y))
#define GLSL_NOT(x) (!(x))

#define GLSL_TEXTURE(tex, uv) tex.Sample(glslSampler, (uv))
#define GLSL_TEXTURE_BIAS(tex, uv, bias) tex.SampleBias(glslSampler, (uv), (bias))
#define GLSL_TEXTURE_LEVEL(tex, uv, lod) tex.SampleLevel(glslSampler, (uv), (lod))
#define GLSL_TEXTURE_GRAD(tex, uv, dx, dy) tex.SampleGrad(glslSampler, (uv), (dx), (dy))
#define GLSL_TEXTURE_S(tex, samp, uv) tex.Sample(samp, (uv))
#define GLSL_TEXTURE_BIAS_S(tex, samp, uv, bias) tex.SampleBias(samp, (uv), (bias))
#define GLSL_TEXTURE_LEVEL_S(tex, samp, uv, lod) tex.SampleLevel(samp, (uv), (lod))
#define GLSL_TEXTURE_GRAD_S(tex, samp, uv, dx, dy) tex.SampleGrad(samp, (uv), (dx), (dy))
#define GLSL_TEXEL_FETCH(tex, p, lod) tex.Load(int3(int2(p), int(lod)))
float GLSL_MOD(float x, float y) { return x - y * floor(x / y); }
float2 GLSL_MOD(float2 x, float2 y) { return x - y * floor(x / y); }
float3 GLSL_MOD(float3 x, float3 y) { return x - y * floor(x / y); }
float4 GLSL_MOD(float4 x, float4 y) { return x - y * floor(x / y); }
float2 GLSL_MOD(float2 x, float y) { return x - y * floor(x / y); }
float3 GLSL_MOD(float3 x, float y) { return x - y * floor(x / y); }
float4 GLSL_MOD(float4 x, float y) { return x - y * floor(x / y); }

// Relaxed scalar conversion for real-world shader-golf sources. Valid scalar
// GLSL int(...) casts preserve normal behavior; vector inputs select x so
// otherwise-undefined vector-to-scalar source remains deterministic in BO3.
int GLSL_INT(float x) { return (int)x; }
int GLSL_INT(int x) { return x; }
int GLSL_INT(uint x) { return (int)x; }
int GLSL_INT(bool x) { return x ? 1 : 0; }
int GLSL_INT(float2 x) { return (int)x.x; }
int GLSL_INT(float3 x) { return (int)x.x; }
int GLSL_INT(float4 x) { return (int)x.x; }
int GLSL_INT(int2 x) { return x.x; }
int GLSL_INT(int3 x) { return x.x; }
int GLSL_INT(int4 x) { return x.x; }
int GLSL_INT(uint2 x) { return (int)x.x; }
int GLSL_INT(uint3 x) { return (int)x.x; }
int GLSL_INT(uint4 x) { return (int)x.x; }
int GLSL_INT(bool2 x) { return x.x ? 1 : 0; }
int GLSL_INT(bool3 x) { return x.x ? 1 : 0; }
int GLSL_INT(bool4 x) { return x.x ? 1 : 0; }

int GLSL_WRAP_INDEX_2(int i) { int j = i % 2; return j < 0 ? j + 2 : j; }
int GLSL_WRAP_INDEX_3(int i) { int j = i % 3; return j < 0 ? j + 3 : j; }
int GLSL_WRAP_INDEX_4(int i) { int j = i % 4; return j < 0 ? j + 4 : j; }
void GLSL_SET_VEC2(inout float2 v, int i, float x) { i=GLSL_WRAP_INDEX_2(i); if(i==0) v.x=x; else v.y=x; }
void GLSL_SET_VEC3(inout float3 v, int i, float x) { i=GLSL_WRAP_INDEX_3(i); if(i==0) v.x=x; else if(i==1) v.y=x; else v.z=x; }
void GLSL_SET_VEC4(inout float4 v, int i, float x) { i=GLSL_WRAP_INDEX_4(i); if(i==0) v.x=x; else if(i==1) v.y=x; else if(i==2) v.z=x; else v.w=x; }
)GLSLHLSL");
        if(emitFullLibrary || converted.trimmed().isEmpty())
            return allHelpers;

        // Clean BO3 HLSL output: emit only compatibility symbols that the
        // converted shader actually references. This reduces generated source,
        // FXC parse work, and collision risk with Treyarch headers.
        QSet<QString> required;
        QRegularExpression useRe(R"(\b(GLSL_[A-Z0-9_]+)\b)");
        auto useIt = useRe.globalMatch(converted);
        while(useIt.hasNext()) required.insert(useIt.next().captured(1));

        // Internal helper dependencies.
        if(required.contains("GLSL_INVERSE")) required.insert("GLSL_DET3");
        if(required.contains("GLSL_SET_VEC2")) required.insert("GLSL_WRAP_INDEX_2");
        if(required.contains("GLSL_SET_VEC3")) required.insert("GLSL_WRAP_INDEX_3");
        if(required.contains("GLSL_SET_VEC4")) required.insert("GLSL_WRAP_INDEX_4");

        QStringList output;
        const QStringList lines = allHelpers.split('\n');
        for(int i = 0; i < lines.size(); ++i)
        {
            const QString line = lines[i];
            const QString trimmed = line.trimmed();
            if(trimmed.isEmpty()) continue;
            if(trimmed.startsWith("//")) continue;

            const auto macroMatch = QRegularExpression(R"(^#define\s+(GLSL_[A-Z0-9_]+)\b)").match(trimmed);
            if(macroMatch.hasMatch())
            {
                if(required.contains(macroMatch.captured(1))) output << line;
                continue;
            }

            const auto variableMatch = QRegularExpression(R"(\b(GLSL_[A-Z0-9_]+)\b)").match(trimmed);
            const auto functionMatch = QRegularExpression(
                R"(^\s*(?:void|float(?:[234](?:x[234])?)?|int(?:[234])?|uint(?:[234])?|bool(?:[234])?)\s+(GLSL_[A-Z0-9_]+)\s*\()")
                .match(line);
            if(functionMatch.hasMatch())
            {
                const QString name = functionMatch.captured(1);
                QStringList block{line};
                int braces = line.count('{') - line.count('}');
                bool sawOpeningBrace = line.contains('{');

                // Some compatibility helpers put the opening brace on the next
                // line (for example GLSL_INVERSE and GLSL_TEXTURE_SIZE).  The
                // old dependency-pruner treated a zero brace count on the
                // signature line as a complete one-line function and emitted
                // only the signature, leaving invalid HLSL behind.  Keep
                // consuming until the definition's opening brace is found, then
                // continue through the matching closing brace.
                while(!sawOpeningBrace && i + 1 < lines.size())
                {
                    const QString next = lines[++i];
                    block << next;
                    braces += next.count('{') - next.count('}');
                    if(next.contains('{'))
                        sawOpeningBrace = true;
                    // Defensive escape for a declaration/prototype.  The helper
                    // library currently contains definitions, but do not swallow
                    // unrelated following source if a prototype is added later.
                    if(!sawOpeningBrace && next.contains(';'))
                        break;
                }
                while(sawOpeningBrace && braces > 0 && i + 1 < lines.size())
                {
                    const QString next = lines[++i];
                    block << next;
                    braces += next.count('{') - next.count('}');
                }
                if(required.contains(name)) output << block;
                continue;
            }

            // GLSL_FRAGCOORD is the only standalone compatibility variable.
            if(variableMatch.hasMatch())
            {
                if(required.contains(variableMatch.captured(1))) output << line;
                continue;
            }
        }
        return output.join("\n") + (output.isEmpty() ? QString() : QString("\n"));
    }

    QString pruneUnusedConvertedGlslGlobals(QString source, QStringList& notes) const
    {
        QStringList lines = source.split('\n');
        int braceDepth = 0;
        int removed = 0;
        const QRegularExpression declRe(
            R"(^\s*static\s+(?:const\s+)?(?:float|int|uint|bool)(?:[234](?:x[234])?)?\s+([A-Za-z_]\w*)\s*=\s*([^;]+);\s*$)");
        for(QString& line : lines)
        {
            if(braceDepth == 0)
            {
                const auto m = declRe.match(line);
                if(m.hasMatch())
                {
                    const QString name = m.captured(1);
                    const QString initializer = m.captured(2).trimmed();
                    // Only remove side-effect-free literal/constructor-free
                    // initializers. Function calls can intentionally mutate state.
                    if(!initializer.contains('('))
                    {
                        const QRegularExpression useRe(QString(R"(\b%1\b)").arg(QRegularExpression::escape(name)));
                        int uses = 0;
                        auto it = useRe.globalMatch(source);
                        while(it.hasNext()) { it.next(); ++uses; if(uses > 1) break; }
                        if(uses == 1)
                        {
                            line.clear();
                            ++removed;
                        }
                    }
                }
            }
            braceDepth += line.count('{') - line.count('}');
        }
        if(removed > 0)
            notes << QString("Removed %1 unused side-effect-free converted GLSL global(s).").arg(removed);
        return lines.join("\n");
    }

    QString pruneUnusedConvertedGlslFunctions(QString source, QStringList& notes) const
    {
        struct FunctionDef
        {
            QString name;
            int start = 0;
            int end = 0;
            QString body;
        };

        const QRegularExpression defRe(
            R"((?:^|[;}
])\s*(?:static\s+|inline\s+|const\s+)*(?:void|float(?:[234](?:x[234])?)?|half(?:[234](?:x[234])?)?|int(?:[234])?|uint(?:[234])?|bool(?:[234])?|(?!(?:if|else|for|while|switch|do|return|case|default|discard)\b)[A-Za-z_]\w*)\s+(BO3GLSL_USER_[A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{)",
            QRegularExpression::MultilineOption);
        QVector<FunctionDef> defs;
        auto it = defRe.globalMatch(source);
        while(it.hasNext())
        {
            const auto m = it.next();
            const int brace = source.indexOf('{', m.capturedStart());
            if(brace < 0) continue;
            const int close = glslFindMatchingForward(source, brace);
            if(close < 0) continue;
            int definitionStart = static_cast<int>(m.capturedStart());
            if(definitionStart < source.size() &&
               (source[definitionStart] == ';' || source[definitionStart] == '}' || source[definitionStart] == '\n'))
                ++definitionStart;
            defs.push_back({m.captured(1), definitionStart, close + 1,
                            source.mid(brace + 1, close - brace - 1)});
        }
        if(defs.isEmpty()) return source;

        QSet<QString> allNames;
        for(const auto& def : defs) allNames.insert(def.name);
        QSet<QString> reachable;
        QString roots;

        // mainImage plus top-level initializers are roots. Strip user helper
        // definitions from the top-level scan so definitions don't make one
        // another appear reachable merely because they exist.
        roots = source;
        QVector<QPair<int,int>> ranges;
        for(const auto& def : defs) ranges.push_back({def.start, def.end});
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
        for(const auto& r : ranges) roots.remove(r.first, r.second - r.first);

        auto addCalls = [&](const QString& text, QSet<QString>& set)
        {
            for(const QString& name : allNames)
            {
                if(text.contains(QRegularExpression(QString(R"(\b%1\s*\()").arg(QRegularExpression::escape(name)))))
                    set.insert(name);
            }
        };
        addCalls(roots, reachable);

        // Treat the fragment/vertex entry bodies as explicit reachability roots.
        // In most shaders they are already present in `roots`, but doing this
        // independently makes the optimizer robust against compact formatting and
        // future top-level scanning changes.  A converter entry must never lose a
        // helper merely because the generic root scan failed to see the call.
        const QStringList entryNames = {"mainImage", "main", "ps_main", "vs_main"};
        for(const QString& entryName : entryNames)
        {
            const QRegularExpression entryRe(
                QString(R"(\b(?:void|float(?:[234])?|half(?:[234])?|int(?:[234])?|uint(?:[234])?|bool(?:[234])?)\s+%1\s*\([^;{}]*\)\s*\{)")
                    .arg(QRegularExpression::escape(entryName)),
                QRegularExpression::CaseInsensitiveOption);
            const auto entry = entryRe.match(source);
            if(!entry.hasMatch()) continue;
            const int open = source.indexOf('{', entry.capturedStart());
            const int close = open >= 0 ? glslFindMatchingForward(source, open) : -1;
            if(open >= 0 && close > open)
                addCalls(source.mid(open + 1, close - open - 1), reachable);
        }

        bool changed = true;
        while(changed)
        {
            changed = false;
            const auto current = reachable.values();
            for(const QString& name : current)
            {
                for(const auto& def : defs)
                {
                    if(def.name != name) continue;
                    QSet<QString> called;
                    addCalls(def.body, called);
                    for(const QString& callee : called)
                    {
                        if(!reachable.contains(callee))
                        {
                            reachable.insert(callee);
                            changed = true;
                        }
                    }
                }
            }
        }

        QVector<QPair<int,int>> removeRanges;
        QStringList removed;
        for(const auto& def : defs)
        {
            if(!reachable.contains(def.name))
            {
                removeRanges.push_back({def.start, def.end});
                removed << def.name;
            }
        }
        if(removeRanges.isEmpty()) return source;
        std::sort(removeRanges.begin(), removeRanges.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
        for(const auto& r : removeRanges)
            source.remove(r.first, r.second - r.first);
        removed.removeDuplicates();
        notes << QString("Removed %1 unreachable converted GLSL helper function definition(s): %2.")
                     .arg(removeRanges.size()).arg(removed.join(", "));
        return source;
    }

    QString namespaceConvertedGlslUserFunctions(QString source, QStringList& notes) const
    {
        // BO3's stock shader headers expose a number of short global helper names
        // (max3/min3/max4/min4, luminance, etc.). Shadertoy/GLSL authors are free
        // to use the same names, but once a converted shader is embedded in a BO3
        // Material/Sky/PostFX package FXC sees both definitions and reports X3003.
        //
        // Keep mainImage as the converter entry contract, but namespace every other
        // user-authored function before any BO3 wrapper/includes are added. Calls are
        // renamed alongside declarations, so overload sets remain intact while stock
        // BO3 helper names can coexist safely.
        const QRegularExpression functionDefRe(
            R"((?:^|[;}\n])\s*(?:static\s+|inline\s+|const\s+)*(?:void|float(?:[234](?:x[234])?)?|half(?:[234](?:x[234])?)?|int(?:[234])?|uint(?:[234])?|bool(?:[234])?|(?!(?:if|else|for|while|switch|do|return|case|default|discard)\b)[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{)",
            QRegularExpression::MultilineOption);

        // This pass intentionally accepts user-defined struct return types, so the
        // final return-type alternative above must remain generic.  That also means
        // control-flow text can superficially resemble a declaration to a regex
        // (notably `else if (...) {`).  Never allow GLSL/HLSL control keywords to
        // enter the user-function namespace set.
        const QSet<QString> nonFunctionKeywords = {
            "if", "else", "for", "while", "do", "switch", "case", "default",
            "return", "break", "continue", "discard"
        };
        // The generic return-type branch above is intentionally permissive so user
        // structs can be returned from helpers. A malformed/ambiguous match must
        // never be allowed to promote an HLSL type token into the function-name
        // set, otherwise the global rename below can turn every float4(...)
        // constructor into BO3GLSL_USER_float4(...).
        const QSet<QString> nonFunctionTypeNames = {
            "void", "bool", "bool2", "bool3", "bool4",
            "int", "int2", "int3", "int4",
            "uint", "uint2", "uint3", "uint4",
            "half", "half2", "half3", "half4",
            "float", "float2", "float3", "float4",
            "float2x2", "float2x3", "float2x4",
            "float3x2", "float3x3", "float3x4",
            "float4x2", "float4x3", "float4x4",
            "double", "min16float", "min10float",
            "texture1d", "texture1darray", "texture2d", "texture2darray",
            "texture2dms", "texture2dmsarray", "texture3d", "texturecube",
            "texturecubearray", "buffer", "structuredbuffer", "byteaddressbuffer",
            "rwtexture1d", "rwtexture2d", "rwtexture3d", "rwbuffer",
            "rwstructuredbuffer", "rwbyteaddressbuffer",
            "samplerstate", "samplercomparisonstate"
        };

        QSet<QString> functionNames;
        auto it = functionDefRe.globalMatch(source);
        while(it.hasNext())
        {
            const QString name = it.next().captured(1);
            const QString lowerName = name.toLower();
            if(nonFunctionKeywords.contains(lowerName) ||
               nonFunctionTypeNames.contains(lowerName))
                continue;
            if(name.compare("mainImage", Qt::CaseInsensitive) == 0 ||
               name.compare("main", Qt::CaseInsensitive) == 0 ||
               name.compare("ps_main", Qt::CaseInsensitive) == 0 ||
               name.compare("vs_main", Qt::CaseInsensitive) == 0)
                continue;
            functionNames.insert(name);
        }

        if(functionNames.isEmpty()) return source;

        QStringList renamed;
        QStringList ordered = functionNames.values();
        std::sort(ordered.begin(), ordered.end(), [](const QString& a, const QString& b)
        {
            return a.size() > b.size();
        });
        for(const QString& name : ordered)
        {
            QString replacement = "BO3GLSL_USER_" + name;
            int suffix = 2;
            while(source.contains(QRegularExpression(
                QString(R"(\b%1\b)").arg(QRegularExpression::escape(replacement)))))
                replacement = QString("BO3GLSL_USER_%1_%2").arg(name).arg(suffix++);

            source.replace(QRegularExpression(
                QString(R"(\b%1\b)").arg(QRegularExpression::escape(name))), replacement);
            renamed << QString("%1 -> %2").arg(name, replacement);
        }

        notes << QString("Namespaced %1 user GLSL helper function(s) for BO3 header compatibility (%2).")
                     .arg(renamed.size())
                     .arg(renamed.join(", "));

        // Dead-code elimination is an optimization only; it must never be able to
        // invalidate the converted shader. Keep a structurally complete namespaced
        // copy and validate the pruned result before accepting it.
        const QString namespacedSource = source;
        const bool hadMainImage = namespacedSource.contains(QRegularExpression(
            R"(\bmainImage\s*\()", QRegularExpression::CaseInsensitiveOption));
        const bool hadClassicMain = namespacedSource.contains(QRegularExpression(
            R"(\bvoid\s+main\s*\()", QRegularExpression::CaseInsensitiveOption));

        QString pruned = pruneUnusedConvertedGlslFunctions(namespacedSource, notes);
        bool unsafePrune = false;
        QStringList unsafeReasons;
        if(hadMainImage && !pruned.contains(QRegularExpression(
               R"(\bmainImage\s*\()", QRegularExpression::CaseInsensitiveOption)))
        {
            unsafePrune = true;
            unsafeReasons << "mainImage entry was removed";
        }
        if(hadClassicMain && !pruned.contains(QRegularExpression(
               R"(\bvoid\s+main\s*\()", QRegularExpression::CaseInsensitiveOption)))
        {
            unsafePrune = true;
            unsafeReasons << "main entry was removed";
        }

        // Every remaining BO3GLSL_USER_* call must still have at least one
        // function definition. This catches transitive reachability mistakes before
        // they become an FXC X3004 undeclared-identifier error.
        QSet<QString> calledHelpers;
        const QRegularExpression helperCallRe(R"(\b(BO3GLSL_USER_[A-Za-z_]\w*)\s*\()");
        auto callIt = helperCallRe.globalMatch(pruned);
        while(callIt.hasNext()) calledHelpers.insert(callIt.next().captured(1));
        for(const QString& helper : calledHelpers)
        {
            const QRegularExpression defRe(
                QString(R"(\b(?:void|float(?:[234](?:x[234])?)?|half(?:[234](?:x[234])?)?|int(?:[234])?|uint(?:[234])?|bool(?:[234])?|(?!(?:if|else|for|while|switch|do|return|case|default|discard)\b)[A-Za-z_]\w*)\s+%1\s*\([^;{}]*\)\s*\{)")
                    .arg(QRegularExpression::escape(helper)));
            if(!defRe.match(pruned).hasMatch())
            {
                unsafePrune = true;
                unsafeReasons << QString("%1 call lost its definition").arg(helper);
            }
        }

        if(unsafePrune)
        {
            notes << QString("Skipped converted-GLSL dead-code pruning because its structural safety check failed (%1). Kept the complete namespaced shader instead.")
                         .arg(unsafeReasons.join("; "));
            source = namespacedSource;
        }
        else
        {
            source = pruned;
        }
        return pruneUnusedConvertedGlslGlobals(source, notes);
    }

    struct GlslPostFxSceneOrientationAnalysis
    {
        bool keepSceneY = false;
        QString reason;
    };

    GlslPostFxSceneOrientationAnalysis analyzeGlslPostFxSceneOrientation(const QString& source) const
    {
        GlslPostFxSceneOrientationAnalysis analysis;
        if(source.trimmed().isEmpty())
        {
            analysis.reason = "No GLSL source is available; defaulting to Shadertoy lower-left scene UVs.";
            return analysis;
        }

        // Shadertoy fragCoord is lower-left oriented, but many screen/image effects
        // explicitly invert the UV used to sample iChannel0. Detect only inversions
        // that feed the primary scene channel; unrelated noise/mask UV flips must not
        // suppress the BO3 resolvedScene orientation bridge.
        const QRegularExpression directYFlip(
            R"(\b([A-Za-z_]\w*)\s*\.\s*y\s*=\s*1(?:\.0*)?\s*-\s*\1\s*\.\s*y\b)",
            QRegularExpression::CaseInsensitiveOption);
        auto flipIt = directYFlip.globalMatch(source);
        while(flipIt.hasNext())
        {
            const QRegularExpressionMatch flip = flipIt.next();
            const QString variable = flip.captured(1);
            const QRegularExpression sceneSample(
                QString(R"(\btexture(?:Lod|Grad|Proj)?\s*\(\s*iChannel0\s*,[^;\n]*\b%1\b)")
                    .arg(QRegularExpression::escape(variable)),
                QRegularExpression::CaseInsensitiveOption);
            if(source.contains(sceneSample))
            {
                analysis.keepSceneY = true;
                analysis.reason = QString("Detected '%1.y = 1 - %1.y' on the UV used to sample iChannel0; resolvedScene should keep that authored upper-left orientation (no additional adapter flip).")
                    .arg(variable);
                return analysis;
            }
        }

        const QRegularExpression inlineSceneYFlip(
            R"(\btexture(?:Lod|Grad|Proj)?\s*\(\s*iChannel0\s*,[^;\n]*\b(?:vec2|float2)\s*\([^,]+,\s*1(?:\.0*)?\s*-\s*[A-Za-z_]\w*\s*\.\s*y\s*\))",
            QRegularExpression::CaseInsensitiveOption);
        if(source.contains(inlineSceneYFlip))
        {
            analysis.keepSceneY = true;
            analysis.reason = "Detected an inline 1-Y inversion in the iChannel0 sample coordinate; resolvedScene should keep that authored upper-left orientation (no additional adapter flip).";
            return analysis;
        }

        analysis.reason = "No explicit Y inversion was detected on the iChannel0 scene sample; use Shadertoy's lower-left scene convention and flip resolvedScene once at the BO3/D3D sampling boundary.";
        return analysis;
    }

    QString makeBo3PostfxFromGlsl(const QString& converted, const QSet<int>& channels,
                                  const QStringList& varyingAliases,
                                  int sceneOrientationMode = 0,
                                  const QString& originalGlsl = QString()) const
    {
        QString resources;
        // Do not manufacture legacy frameBuffer/glslSampler declarations when
        // the converted pass does not actually use them. FXC strips unused
        // resources, while BO3 still attempts explicit techset bindings; the
        // old unconditional declarations therefore produced false Runtime
        // packages that failed PACKAGE_BINDING_OPTIMIZED_OUT.
        const bool needsFrameBuffer = converted.contains(QRegularExpression(
            R"(\bframeBuffer\b)", QRegularExpression::CaseInsensitiveOption));
        const bool needsGenericSampler = converted.contains(QRegularExpression(
            R"(\bGLSL_TEXTURE(?:_BIAS|_LEVEL|_GRAD)?\s*\()",
            QRegularExpression::CaseInsensitiveOption)) ||
            converted.contains(QRegularExpression(
                R"(\bglslSampler\b)", QRegularExpression::CaseInsensitiveOption));
        if(needsFrameBuffer)
            resources += "Texture2D<float4> frameBuffer : register(t0);\n";
        for(int channel : channels)
        {
            const int slot = channel + 2; // t0/t1 remain reserved for scene/depth; iChannel0..3 use t2..t5.
            resources += QString("Texture2D<float4> iChannel%1 : register(t%2);\n").arg(channel).arg(slot);
        }
        if(needsGenericSampler)
            resources += "SamplerState glslSampler : register(s1);\n";
        for(int channel : channels)
        {
            const int samplerSlot = channel + 2;
            resources += QString("SamplerState glslSampler%1 : register(s%2);\n").arg(channel).arg(samplerSlot);
        }

        QString aliases;
        for(const QString& name : varyingAliases)
            aliases += QString("#define %1 (GLSL_FRAGCOORD.xy / max(iResolution.xy, float2(1.0, 1.0)))\n").arg(name);

        bool keepSceneY = false;
        QString sceneOrientationReason;
        if(sceneOrientationMode == 2)
        {
            keepSceneY = true;
            sceneOrientationReason = "Forced already-upper-left scene orientation.";
        }
        else if(sceneOrientationMode == 1)
        {
            keepSceneY = false;
            sceneOrientationReason = "Forced Shadertoy lower-left scene orientation.";
        }
        else
        {
            const GlslPostFxSceneOrientationAnalysis orientation =
                analyzeGlslPostFxSceneOrientation(originalGlsl);
            keepSceneY = orientation.keepSceneY;
            sceneOrientationReason = orientation.reason;
        }
        const QString orientationMarker = QString(
            "// BO3_PREVIEWER_POSTFX_SCENE_ORIENTATION: %1\n"
            "// %2\n")
            .arg(keepSceneY ? "KEEP_Y" : "FLIP_Y", sceneOrientationReason);

        return orientationMarker + QStringLiteral("#include \"postfx/postfx_common.h\"\n\n") + resources + QStringLiteral(R"POSTFX(
#define iTime (GetTime())
#define iTimeDelta (1.0 / 60.0)
#define iFrameRate 60.0
#define iFrame 0
#define iResolution (float3(PostFx_GetRenderTargetSize().xy, 1.0))
static const float4 iMouse = float4(0.0, 0.0, 0.0, 0.0);
static const float4 iDate = float4(0.0, 0.0, 0.0, 0.0);
)POSTFX") + glslCompatibilityHelpers(converted + "\nGLSL_FRAGCOORD") + "\n" + aliases + QStringLiteral(R"POSTFX(
struct VertexInput
{
    float3 position : POSITION;
    float2 texCoords : TEXCOORD0;
};
struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texCoords : TEXCOORD0;
};
PixelInput vs_main(const VertexInput vertex, const uint instance : INSTANCE_SEMANTIC)
{
    PixelInput pixel;
    PostFx_GenerateFullscreenQuad(vertex.position, vertex.texCoords, instance, pixel.position, pixel.texCoords);
    return pixel;
}

// -----------------------------------------------------------------------------
// Converted GLSL
// -----------------------------------------------------------------------------
)POSTFX") + converted + QStringLiteral(R"POSTFX(
float4 ps_main(const PixelInput input) : SV_TARGET0
{
    // ShaderToy/gl_FragCoord uses a lower-left origin; D3D SV_POSITION uses upper-left.
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(fragColor, fragCoord);
    return fragColor;
}
)POSTFX");
    }

    QString makeBo3MaterialFromGlsl(const QString& converted, const QSet<int>& channels,
                                      const QStringList& varyingAliases, int surfaceMode) const
    {
        QString resources;
        const bool needsGenericSampler = converted.contains(QRegularExpression(
            R"(\bGLSL_TEXTURE(?:_BIAS|_LEVEL|_GRAD)?\s*\()",
            QRegularExpression::CaseInsensitiveOption)) ||
            converted.contains(QRegularExpression(
                R"(\bglslSampler\b)", QRegularExpression::CaseInsensitiveOption));
        if(needsGenericSampler)
            resources += "SamplerState glslSampler : register(s1);\n";
        for(int channel : channels)
        {
            const int slot = channel + 2; // t0/t1 stay available for BO3 preview resources.
            resources += QString("Texture2D<float4> iChannel%1 : register(t%2);\n").arg(channel).arg(slot);
            resources += QString("SamplerState glslSampler%1 : register(s%2);\n").arg(channel).arg(slot);
        }

        QString aliases;
        for(const QString& name : varyingAliases)
            aliases += QString("#define %1 (GLSL_FRAGCOORD.xy / max(iResolution.xy, float2(1.0, 1.0)))\n").arg(name);

        const QString surfaceTag = surfaceMode == 1 ? "CUTOUT" : (surfaceMode == 2 ? "TRANSPARENT" : "OPAQUE");
        const QString previewClip = surfaceMode == 1
            ? QStringLiteral(R"MAT(    // Preview-only cutout. BO3 export uses the Material Alpha Cutoff control instead.
#ifndef BO3_CUSTOM_MATERIAL_EXPORT
    clip(saturate(fragColor.a) - 0.5);
#endif
)MAT")
            : QString();

        // Converted GLSL Materials are fundamentally image-space shaders being
        // projected onto 3D meshes. The wrapper keeps the authored image intact
        // through the interior, repairs only a narrow repeated-U longitude band,
        // and gives sphere-like geometry a small longitude-independent polar cap.
        // That removes both closed-mesh meridian seams and the UV-sphere pole
        // pinwheel without globally crossfading the shader against a shifted copy.
        // The virtual fragCoord also stays upright; no V flip is introduced.
        const QString materialEntry = QStringLiteral(R"MAT(
float4 BO3GLSL_EvaluateMaterialMainImage(float2 fragCoord)
{
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    float4 c = float4(0.0, 0.0, 0.0, 0.0);
    mainImage(c, fragCoord);
    return c;
}

float BO3GLSL_PeriodicMaterialUWeight(float u)
{
    // Quintic smootherstep. It reaches both endpoints with zero slope, which is
    // what we need when two rasterized sides of a closed-mesh seam meet.
    u = saturate(u);
    return u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
}

float4 BO3GLSL_EvaluateMaterialSeamSafe(float2 uv)
{
    // Keep the authored image untouched over almost the whole surface. Only the
    // narrow longitude band is repaired. This is materially different from the
    // older full-interval shifted-image crossfade, which could create a second
    // visible transition through the middle of strongly non-periodic shaders.
    float materialU = frac(uv.x);
    float materialV = uv.y;
    float2 fragCoord = float2(materialU, materialV) * BO3_GLSL_MATERIAL_RESOLUTION;
    float4 authoredColor = BO3GLSL_EvaluateMaterialMainImage(fragCoord);

    const float BO3_GLSL_LONGITUDE_BLEND = 0.035;
    float seamDistance = min(materialU, 1.0 - materialU);
    if(seamDistance < BO3_GLSL_LONGITUDE_BLEND)
    {
        // Use the average of the actual left/right authored edge as the seam
        // anchor. Both U=0 and U=1 therefore converge to exactly the same color,
        // and smootherstep makes the first derivative converge to zero too.
        float2 edge0FragCoord = float2(0.0, materialV) * BO3_GLSL_MATERIAL_RESOLUTION;
        float2 shiftedFragCoord = float2(BO3_GLSL_MATERIAL_RESOLUTION.x,
                                        materialV * BO3_GLSL_MATERIAL_RESOLUTION.y);
        float4 edge0Color = BO3GLSL_EvaluateMaterialMainImage(edge0FragCoord);
        float4 edge1Color = BO3GLSL_EvaluateMaterialMainImage(shiftedFragCoord);
        float4 seamColor = 0.5 * (edge0Color + edge1Color);
        float seamWeight = BO3GLSL_PeriodicMaterialUWeight(
            seamDistance / BO3_GLSL_LONGITUDE_BLEND);
        authoredColor = lerp(seamColor, authoredColor, seamWeight);
    }

    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    return authoredColor;
}

float BO3GLSL_MaterialSphereFactor(const MaterialSurfaceInput input)
{
    // The built-in sphere is centered at the preview origin with unit radius.
    // Gate polar repair to sphere-like geometry so Plane/Card previews retain
    // their exact top and bottom image rows. The extra radial-normal test also
    // keeps ordinary flat/cube faces from being mistaken for a sphere.
    float3 p = input.worldPosition.xyz;
    float pLenSq = dot(p, p);
    float nLenSq = dot(input.normal.xyz, input.normal.xyz);
    if(pLenSq < 1.0e-8 || nLenSq < 1.0e-8)
        return 0.0;

    float pLen = sqrt(pLenSq);
    float3 radial = p / pLen;
    float3 n = input.normal.xyz * rsqrt(nLenSq);
    float radialMatch = smoothstep(0.9975, 0.9995, abs(dot(radial, n)));
    float unitRadiusMatch = 1.0 - smoothstep(0.03, 0.12, abs(pLen - 1.0));
    return saturate(radialMatch * unitRadiusMatch);
}

float4 BO3GLSL_StabilizeMaterialPoles(float2 uv,
                                      float4 authoredColor,
                                      float sphereFactor)
{
    // Equirectangular coordinates necessarily collapse every longitude to one
    // geometric point at V=0/1. If the source is an arbitrary 2D mainImage that
    // produces the familiar pinwheel/star at a sphere pole. Fade the last few
    // latitude degrees into a longitude-independent cap sampled just inside the
    // pole. This preserves the panorama across the rest of the sphere while
    // removing the singular visual collapse at the cap itself.
    if(sphereFactor <= 0.001)
        return authoredColor;

    float materialV = saturate(uv.y);
    const float BO3_GLSL_POLAR_CAP = 0.060;
    float poleDistance = min(materialV, 1.0 - materialV);
    if(poleDistance >= BO3_GLSL_POLAR_CAP)
        return authoredColor;

    float capV = materialV < 0.5
        ? BO3_GLSL_POLAR_CAP
        : 1.0 - BO3_GLSL_POLAR_CAP;

    // Two fixed longitudes give a stable, representative cap color without
    // multiplying the cost of the source shader everywhere on the sphere.
    float4 capA = BO3GLSL_EvaluateMaterialSeamSafe(float2(0.25, capV));
    float4 capB = BO3GLSL_EvaluateMaterialSeamSafe(float2(0.75, capV));
    float4 capColor = 0.5 * (capA + capB);
    float authoredWeight = BO3GLSL_PeriodicMaterialUWeight(
        poleDistance / BO3_GLSL_POLAR_CAP);
    float poleInfluence = sphereFactor * (1.0 - authoredWeight);
    return lerp(authoredColor, capColor, saturate(poleInfluence));
}

float4 ps_main(const MaterialSurfaceInput input) : SV_TARGET0
{
    // The original 2D shader stays upright. Longitude is made C1-continuous in
    // a narrow edge band, and sphere-like geometry additionally gets a stable
    // polar cap so neither the UV seam nor the north/south pinwheel is visible.
    float2 surfaceUv = input.texCoords.xy;
    float4 fragColor = BO3GLSL_EvaluateMaterialSeamSafe(surfaceUv);
    float sphereFactor = BO3GLSL_MaterialSphereFactor(input);
    fragColor = BO3GLSL_StabilizeMaterialPoles(surfaceUv, fragColor, sphereFactor);

    float materialU = frac(surfaceUv.x);
    float2 fragCoord = float2(materialU, surfaceUv.y) * BO3_GLSL_MATERIAL_RESOLUTION;
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
)MAT");

        return QString("// BO3_PREVIEWER_MATERIAL_SURFACE: %1\n").arg(surfaceTag) + resources + QStringLiteral(R"MAT(
// Material / Surface GLSL wrapper.
// The original mainImage() is evaluated across mesh UVs instead of the screen.
// Export with: Export to BO3 -> Material -> Custom HLSL Material.
// For grass/cards choose Alpha Cutout (shader alpha).
//
// The converter output is also a valid standalone preview pixel shader. The
// final BO3 material adapter defines BO3_CUSTOM_MATERIAL_EXPORT before embedding
// this source, so Runtime/TOOLSGFX builds use BO3's real gameTime from
// lib/globals.hlsl instead of this preview-only constant buffer.
#ifndef BO3_CUSTOM_MATERIAL_EXPORT
cbuffer BO3GlslMaterialPreviewGlobals : register(b13)
{
    float4 gameTime;
};
#endif
#define iTime (gameTime.w)
#define iTimeDelta (1.0 / 60.0)
#define iFrameRate 60.0
#define iFrame 0
static const float2 BO3_GLSL_MATERIAL_RESOLUTION = float2(1024.0, 1024.0);
#define iResolution (float3(BO3_GLSL_MATERIAL_RESOLUTION, 1.0))
static const float4 iMouse = float4(0.0, 0.0, 0.0, 0.0);
static const float4 iDate = float4(0.0, 0.0, 0.0, 0.0);
)MAT") + glslCompatibilityHelpers(converted + "\nGLSL_FRAGCOORD") + "\n" + aliases + QStringLiteral(R"MAT(
struct MaterialSurfaceInput
{
    float4 position      : SV_POSITION;
    float4 texCoords     : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 normal        : TEXCOORD2;
    float4 tangent       : TEXCOORD3;
    float4 biTangent     : TEXCOORD4;
};

// -----------------------------------------------------------------------------
// Converted GLSL
// -----------------------------------------------------------------------------
)MAT") + converted + materialEntry + previewClip + QStringLiteral(R"MAT(    return fragColor;
}
)MAT");
    }

    struct GlslSkySourceAnalysis
    {
        int recommendedMode = 1; // 1 = image-space/lat-long, 2 = self-camera/view-ray.
        int selfCameraScore = 0;
        QString rayVariable;
        QString reason;
    };

    GlslSkySourceAnalysis analyzeGlslSkySource(const QString& source) const
    {
        GlslSkySourceAnalysis analysis;
        if(source.trimmed().isEmpty())
        {
            analysis.reason = "No render-pass source is available yet.";
            return analysis;
        }

        int score = 0;
        QString rayVariable;
        const QRegularExpression rayDecl(
            R"(\bvec3\s+(rd|rayDir(?:ection)?|viewDir(?:ection)?|ray|dir)\s*=\s*([^;]+);)",
            QRegularExpression::CaseInsensitiveOption);
        auto rayIt = rayDecl.globalMatch(source);
        while(rayIt.hasNext())
        {
            const QRegularExpressionMatch m = rayIt.next();
            const QString rhs = m.captured(2);
            int candidate = 4;
            if(rhs.contains(QRegularExpression(R"(\bnormalize\s*\()", QRegularExpression::CaseInsensitiveOption))) candidate += 2;
            if(rhs.contains(QRegularExpression(R"(\b(?:fragCoord|iResolution|gl_FragCoord)\b)", QRegularExpression::CaseInsensitiveOption))) candidate += 4;
            if(rhs.contains(QRegularExpression(R"(\b(?:cam|camera|look|rot|basis|iMouse)\w*\b)", QRegularExpression::CaseInsensitiveOption))) candidate += 2;
            if(candidate > score)
            {
                score = candidate;
                rayVariable = m.captured(1);
            }
        }

        if(source.contains(QRegularExpression(
               R"(\b(?:setCamera|lookAt|camera\w*|raymarch\w*|rayMarch\w*)\s*\()",
               QRegularExpression::CaseInsensitiveOption)))
            score += 2;
        if(source.contains(QRegularExpression(
               R"(\b(?:rayOrigin|rayDir(?:ection)?|viewDir(?:ection)?|\bro\b|\brd\b)\b)",
               QRegularExpression::CaseInsensitiveOption)))
            score += 2;
        if(source.contains(QRegularExpression(R"(\biMouse\b)", QRegularExpression::CaseInsensitiveOption)))
            score += 1;
        if(source.contains(QRegularExpression(
               R"(\b(?:march|integrate|density|map\w*)\s*\([^;]*\b(?:rd|rayDir|rayDirection)\b)",
               QRegularExpression::CaseInsensitiveOption)))
            score += 2;

        analysis.selfCameraScore = score;
        analysis.rayVariable = rayVariable;
        if(score >= 8 && !rayVariable.isEmpty())
        {
            analysis.recommendedMode = 2;
            analysis.reason = QString("Detected a self-camera/view-ray shader (ray '%1', score %2). BO3 skyDirection should replace the Shadertoy camera ray instead of wrapping mainImage as a panorama.")
                                  .arg(rayVariable).arg(score);
        }
        else
        {
            analysis.recommendedMode = 1;
            analysis.reason = QString("No strong self-camera ray was detected (score %1). Use the normal 2D/lat-long sky wrap, or force 360° / Self-Camera if this shader builds its own view ray under an unusual name.")
                                  .arg(score);
        }
        return analysis;
    }

    struct GlslSelfCameraRewrite
    {
        QString source;
        bool changed = false;
        QString rayVariable;
        int removedCameraUpdates = 0;
    };

    GlslSelfCameraRewrite rewriteGlslSelfCameraSky(const QString& converted) const
    {
        GlslSelfCameraRewrite result;
        result.source = converted;
        if(converted.trimmed().isEmpty()) return result;

        struct Candidate
        {
            qsizetype expressionStart = -1;
            qsizetype expressionLength = 0;
            qsizetype declarationEnd = -1;
            QString name;
            int score = 0;
        };
        Candidate best;

        const int mainImageAt = converted.indexOf(QRegularExpression(
            R"(\bmainImage\s*\()", QRegularExpression::CaseInsensitiveOption));
        const QRegularExpression declarationRe(
            R"(\bfloat3\s+([A-Za-z_]\w*)\s*=\s*([^;]+);)",
            QRegularExpression::CaseInsensitiveOption);
        auto it = declarationRe.globalMatch(converted);
        while(it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const QString name = m.captured(1);
            const QString rhs = m.captured(2);
            int score = 0;
            if(QRegularExpression(R"(^(?:rd|rayDir(?:ection)?|viewDir(?:ection)?|ray|dir)$)",
                    QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) score += 8;
            if(rhs.contains(QRegularExpression(R"(\bnormalize\s*\()", QRegularExpression::CaseInsensitiveOption))) score += 2;
            if(rhs.contains(QRegularExpression(
                   R"(\b(?:fragCoord|GLSL_FRAGCOORD|iResolution)\b)",
                   QRegularExpression::CaseInsensitiveOption))) score += 5;
            if(rhs.contains(QRegularExpression(
                   R"(\b(?:cam|camera|look|rot|basis|iMouse|GLSL_MAT[234])\w*\b)",
                   QRegularExpression::CaseInsensitiveOption))) score += 2;
            if(mainImageAt >= 0 && m.capturedStart() >= mainImageAt) score += 2;

            if(score > best.score)
            {
                best.expressionStart = m.capturedStart(2);
                best.expressionLength = m.capturedLength(2);
                best.declarationEnd = m.capturedEnd();
                best.name = name;
                best.score = score;
            }
        }

        if(best.score < 8 || best.expressionStart < 0 || best.name.isEmpty())
            return result;

        const QString replacementRay = "BO3_ShaderToySkyDirection() /* BO3 view ray */";
        result.source.replace(best.expressionStart, best.expressionLength, replacementRay);
        const qsizetype assignmentSearchOffset = best.declarationEnd +
            (replacementRay.size() - best.expressionLength);
        result.changed = true;
        result.rayVariable = best.name;

        QSet<QString> matrixNames;
        const QRegularExpression matrixDecl(
            R"(\b(?:float2x2|float3x3|float4x4)\s+([A-Za-z_]\w*)\b)",
            QRegularExpression::CaseInsensitiveOption);
        auto matrixIt = matrixDecl.globalMatch(result.source);
        while(matrixIt.hasNext()) matrixNames.insert(matrixIt.next().captured(1));

        struct Span { qsizetype start = 0; qsizetype length = 0; QString original; };
        QVector<Span> spans;
        const QString escaped = QRegularExpression::escape(best.name);
        const QRegularExpression assignmentRe(
            QString(R"((?m)^\s*%1(?:\s*\.\s*[xyzw]{1,4})?\s*(?:=|\*=|\+=|-=)\s*([^;]+);)").arg(escaped),
            QRegularExpression::CaseInsensitiveOption);
        auto assigns = assignmentRe.globalMatch(result.source, qMax<qsizetype>(0, assignmentSearchOffset));
        while(assigns.hasNext())
        {
            const QRegularExpressionMatch m = assigns.next();
            const QString rhs = m.captured(1);
            bool cameraUpdate = rhs.contains(QRegularExpression(
                R"(\b(?:iMouse|mouse|cam\w*|camera\w*|look\w*|yaw|pitch|orbit|rot\w*|basis\w*|GLSL_MAT[234])\b)",
                QRegularExpression::CaseInsensitiveOption));
            if(!cameraUpdate)
            {
                for(const QString& matrixName : matrixNames)
                {
                    if(rhs.contains(QRegularExpression(QString(R"(\b%1\b)")
                        .arg(QRegularExpression::escape(matrixName)),
                        QRegularExpression::CaseInsensitiveOption)))
                    {
                        cameraUpdate = true;
                        break;
                    }
                }
            }
            if(cameraUpdate)
                spans.push_back({m.capturedStart(), m.capturedLength(), m.captured(0)});
        }

        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b)
        {
            return a.start > b.start;
        });
        for(const Span& span : spans)
        {
            QString replacement = QString("/* BO3_PREVIEWER_SKY_CAMERA_UPDATE_REMOVED: %1 */")
                                      .arg(best.name);
            for(int n = 0; n < span.original.count('\n'); ++n) replacement += '\n';
            result.source.replace(span.start, span.length, replacement);
            ++result.removedCameraUpdates;
        }
        return result;
    }

    QString makeBo3SkyFromGlsl(const QString& converted, const QSet<int>& channels,
                               const QStringList& varyingAliases, int requestedSkySourceMode,
                               const QString& originalGlsl, QStringList* converterNotes = nullptr) const
    {
        QString resources;
        const bool needsGenericSampler = converted.contains(QRegularExpression(
            R"(\bGLSL_TEXTURE(?:_BIAS|_LEVEL|_GRAD)?\s*\()",
            QRegularExpression::CaseInsensitiveOption)) ||
            converted.contains(QRegularExpression(
                R"(\bglslSampler\b)", QRegularExpression::CaseInsensitiveOption));
        if(needsGenericSampler)
            resources += "SamplerState glslSampler : register(s1);\n";
        for(int channel : channels)
        {
            resources += QString("Texture2D<float4> iChannel%1 : register(t%2);\n").arg(channel).arg(channel + 2);
            resources += QString("SamplerState glslSampler%1 : register(s%2);\n").arg(channel).arg(channel + 2);
        }

        QString aliases;
        for(const QString& name : varyingAliases)
            aliases += QString("#define %1 (GLSL_FRAGCOORD.xy / max(iResolution.xy, float2(1.0, 1.0)))\n").arg(name);

        const GlslSkySourceAnalysis analysis = analyzeGlslSkySource(originalGlsl);
        int resolvedMode = requestedSkySourceMode;
        if(resolvedMode == 0) resolvedMode = analysis.recommendedMode;

        QString convertedForSky = converted;
        GlslSelfCameraRewrite selfCameraRewrite;
        if(resolvedMode == 2)
        {
            selfCameraRewrite = rewriteGlslSelfCameraSky(converted);
            if(selfCameraRewrite.changed)
            {
                convertedForSky = selfCameraRewrite.source;
                if(converterNotes)
                {
                    converterNotes->append(QString("Sky source: 360° / Self-Camera. Replaced Shadertoy view ray '%1' with BO3 skyDirection%2.")
                        .arg(selfCameraRewrite.rayVariable)
                        .arg(selfCameraRewrite.removedCameraUpdates > 0
                            ? QString(" and removed %1 camera-orientation update(s)").arg(selfCameraRewrite.removedCameraUpdates)
                            : QString()));
                }
            }
            else
            {
                if(converterNotes)
                    converterNotes->append("Sky source requested 360° / Self-Camera, but no safe camera ray could be identified after GLSL conversion. Falling back to the 2D/lat-long sky wrap; inspect the generated HLSL and choose a recognizable ray variable such as rd/rayDir/viewDir.");
                resolvedMode = 1;
            }
        }
        else if(converterNotes)
        {
            if(requestedSkySourceMode == 0)
                converterNotes->append(QString("Sky source Auto Detect: %1").arg(analysis.reason));
            else
                converterNotes->append("Sky source: 2D / Image-Space. mainImage is wrapped across BO3 skyDirection using a lat-long projection.");
        }

        const QString modeMarker = resolvedMode == 2
            ? QStringLiteral("// BO3_PREVIEWER_SKY_SOURCE: SELF_CAMERA\n// BO3_PREVIEWER_SKY_SELF_CAMERA\n")
            : QStringLiteral("// BO3_PREVIEWER_SKY_SOURCE: IMAGE_SPACE_LATLONG\n");

        const QString commonPreamble = resources + modeMarker + QStringLiteral(R"SKY(
cbuffer PerSceneConsts : register(b1)
{
    float3 sunFogDir : packoffset(c38);
    float4 sunFogColor : packoffset(c39);
    float2 sunFog : packoffset(c40);
    float4 renderTargetSize : packoffset(c44);
    float4 skyMxR : packoffset(c51);
    float4 skyMxG : packoffset(c52);
    float4 skyMxB : packoffset(c53);
    float4 sunMxR : packoffset(c54);
    float4 sunMxG : packoffset(c55);
    float4 sunMxB : packoffset(c56);
    float4 skyRotationTransition : packoffset(c57);
    float4 gameTime : packoffset(c69);
    float4 relHDRExposure : packoffset(c85);
};
struct PixelShaderInput
{
    float4 position : SV_POSITION0;
    float4 skyDirection : TEXCOORD0;
    float4 fogDirection : TEXCOORD1;
};

#define iTime (gameTime.w)
#define iTimeDelta (1.0 / 60.0)
#define iFrameRate 60.0
#define iFrame 0
#define iResolution (float3(max(renderTargetSize.xy, float2(1.0, 1.0)), 1.0))
static const float4 iMouse = float4(0.0, 0.0, 0.0, 0.0);
static const float4 iDate = float4(0.0, 0.0, 0.0, 0.0);
)SKY") + glslCompatibilityHelpers(converted + "\nGLSL_FRAGCOORD") + "\n" + aliases;

        if(resolvedMode == 2)
        {
            return commonPreamble + QStringLiteral(R"SKY(
// BO3 uses Z-up sky directions; most Shadertoy self-camera shaders build rays
// in a Y-up camera/world space with +Z forward. Convert BO3 (X,Y,Z) to the
// usual Shadertoy camera convention (X,Z,Y) before replacing the authored ray.
static float3 BO3_SKY_DIRECTION = float3(0.0, 1.0, 0.0);
float3 BO3_ShaderToySkyDirection()
{
    float3 d = normalize(BO3_SKY_DIRECTION);
    return normalize(float3(d.x, d.z, d.y));
}

// -----------------------------------------------------------------------------
// Converted GLSL - self-camera/view-ray mode
// -----------------------------------------------------------------------------
)SKY") + convertedForSky + QStringLiteral(R"SKY(
float4 ps_main(const PixelShaderInput input) : SV_TARGET0
{
    BO3_SKY_DIRECTION = normalize(input.skyDirection.xyz);

    // Keep real screen-space fragment coordinates for any dithering/noise logic
    // that still uses fragCoord. The 3D viewing direction itself comes directly
    // from BO3 skyDirection through BO3_ShaderToySkyDirection().
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(fragColor, fragCoord);
    return fragColor;
}
)SKY");
        }

        return commonPreamble + QStringLiteral(R"SKY(
// -----------------------------------------------------------------------------
// Converted GLSL - 2D/image-space lat-long wrap
// -----------------------------------------------------------------------------
)SKY") + convertedForSky + QStringLiteral(R"SKY(
float4 BO3GLSL_EvaluateLatLongSkyMainImage(float2 fragCoord)
{
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    float4 c = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(c, fragCoord);
    return c;
}

float BO3GLSL_LatLongPeriodicWeight(float u)
{
    u = saturate(u);
    return u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
}

float4 BO3GLSL_EvaluateLatLongSkySeamSafe(float2 uv)
{
    float skyU = frac(uv.x);
    float skyV = saturate(uv.y);
    float2 fragCoord = float2(skyU, skyV) * iResolution.xy;
    float4 authoredColor = BO3GLSL_EvaluateLatLongSkyMainImage(fragCoord);

    const float BO3_GLSL_SKY_LONGITUDE_BLEND = 0.035;
    float seamDistance = min(skyU, 1.0 - skyU);
    if(seamDistance < BO3_GLSL_SKY_LONGITUDE_BLEND)
    {
        float4 edge0Color = BO3GLSL_EvaluateLatLongSkyMainImage(
            float2(0.0, skyV) * iResolution.xy);
        float4 edge1Color = BO3GLSL_EvaluateLatLongSkyMainImage(
            float2(1.0, skyV) * iResolution.xy);
        float4 seamColor = 0.5 * (edge0Color + edge1Color);
        float seamWeight = BO3GLSL_LatLongPeriodicWeight(
            seamDistance / BO3_GLSL_SKY_LONGITUDE_BLEND);
        authoredColor = lerp(seamColor, authoredColor, seamWeight);
    }

    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    return authoredColor;
}

float4 BO3GLSL_StabilizeLatLongSkyPoles(float2 skyUv, float4 authoredColor)
{
    // A latitude/longitude projection has a true topological singularity at the
    // two poles: every U converges to one direction. Arbitrary image-space GLSL
    // exposes that as a star/pinwheel. Replace only the final polar band with a
    // smooth longitude-independent cap sampled from the adjacent latitude ring.
    float skyV = saturate(skyUv.y);
    const float BO3_GLSL_SKY_POLAR_CAP = 0.060;
    float poleDistance = min(skyV, 1.0 - skyV);
    if(poleDistance >= BO3_GLSL_SKY_POLAR_CAP)
        return authoredColor;

    float capV = skyV < 0.5
        ? BO3_GLSL_SKY_POLAR_CAP
        : 1.0 - BO3_GLSL_SKY_POLAR_CAP;
    float4 capA = BO3GLSL_EvaluateLatLongSkySeamSafe(float2(0.25, capV));
    float4 capB = BO3GLSL_EvaluateLatLongSkySeamSafe(float2(0.75, capV));
    float4 capColor = 0.5 * (capA + capB);
    float authoredWeight = BO3GLSL_LatLongPeriodicWeight(
        poleDistance / BO3_GLSL_SKY_POLAR_CAP);
    return lerp(capColor, authoredColor, authoredWeight);
}

float4 ps_main(const PixelShaderInput input) : SV_TARGET0
{
    float3 rd = normalize(input.skyDirection.xyz);
    const float PI = 3.14159265358979323846;
    float2 skyUv;
    skyUv.x = atan2(rd.y, rd.x) / (2.0 * PI) + 0.5;
    skyUv.y = asin(clamp(rd.z, -1.0, 1.0)) / PI + 0.5;

    // Repair the atan2 meridian only in a narrow edge band, then separately
    // stabilize the latitude singularity. The rest of mainImage is untouched.
    float4 fragColor = BO3GLSL_EvaluateLatLongSkySeamSafe(skyUv);
    fragColor = BO3GLSL_StabilizeLatLongSkyPoles(skyUv, fragColor);
    float2 fragCoord = float2(frac(skyUv.x), saturate(skyUv.y)) * iResolution.xy;
    GLSL_FRAGCOORD = float4(fragCoord, 0.0, 1.0);
    return fragColor;
}
)SKY");
    }


};

const Core& core()
{
    static const Core instance;
    return instance;
}
} // namespace

QString convertGlslSyntax(QString source, QStringList& notes,
                          QSet<int>& usedChannels, QStringList& varyingAliases)
{
    return core().convertGlslSyntax(std::move(source), notes, usedChannels, varyingAliases);
}

QString lowerRuntimeDependentGlslGlobals(QString source, QStringList& notes)
{
    return core().lowerRuntimeDependentGlslGlobals(std::move(source), notes);
}

QString normalizeConstantLoopsForFxc(QString source, QStringList& notes)
{
    return core().normalizeConstantLoopsForFxc(std::move(source), notes);
}

QString glslCompatibilityHelpers(const QString& converted, bool emitFullLibrary)
{
    return core().glslCompatibilityHelpers(converted, emitFullLibrary);
}

QString namespaceConvertedGlslUserFunctions(QString source, QStringList& notes)
{
    return core().namespaceConvertedGlslUserFunctions(std::move(source), notes);
}

GlslPostFxSceneOrientationAnalysis analyzeGlslPostFxSceneOrientation(const QString& source)
{
    const auto result = core().analyzeGlslPostFxSceneOrientation(source);
    GlslPostFxSceneOrientationAnalysis out;
    out.keepSceneY = result.keepSceneY;
    out.reason = result.reason;
    return out;
}

QString makeBo3PostfxFromGlsl(const QString& converted, const QSet<int>& channels,
                              const QStringList& varyingAliases,
                              int sceneOrientationMode, const QString& originalGlsl)
{
    return core().makeBo3PostfxFromGlsl(converted, channels, varyingAliases,
                                        sceneOrientationMode, originalGlsl);
}

QString makeBo3MaterialFromGlsl(const QString& converted, const QSet<int>& channels,
                                const QStringList& varyingAliases, int surfaceMode)
{
    return core().makeBo3MaterialFromGlsl(converted, channels, varyingAliases, surfaceMode);
}

GlslSkySourceAnalysis analyzeGlslSkySource(const QString& source)
{
    const auto result = core().analyzeGlslSkySource(source);
    GlslSkySourceAnalysis out;
    out.recommendedMode = result.recommendedMode;
    out.selfCameraScore = result.selfCameraScore;
    out.rayVariable = result.rayVariable;
    out.reason = result.reason;
    return out;
}

QString makeBo3SkyFromGlsl(const QString& converted, const QSet<int>& channels,
                           const QStringList& varyingAliases, int requestedSkySourceMode,
                           const QString& originalGlsl, QStringList* converterNotes)
{
    return core().makeBo3SkyFromGlsl(converted, channels, varyingAliases,
                                     requestedSkySourceMode, originalGlsl, converterNotes);
}
} // namespace bo3::glsl
