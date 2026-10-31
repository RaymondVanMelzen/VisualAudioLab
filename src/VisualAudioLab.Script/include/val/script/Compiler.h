#pragma once

#include "Program.h"
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>

namespace val::script
{
/** Source-aware compiler failure; the first failing stage never yields a runnable artifact. */
struct Diagnostic
{
    SourceSpan source;
    std::string message;
};
/** Stage result; diagnostics and a value are mutually exclusive. */
template <typename T> struct CompilationResult
{
    std::optional<T> value;
    std::vector<Diagnostic> diagnostics;
};
/** Longest-match lexical unit; identifiers/keywords normalized to ASCII uppercase, authored location retained. */
struct Token
{
    enum class Kind { name, integer, real, symbol, end } kind{};
    std::string text;
    SourceSpan source;
};
/** Bounded ASCII lexer; 64 KiB source, 16384 tokens, decimal literals, // comments. */
class Lexer final
{
public:
    [[nodiscard]] static CompilationResult<std::vector<Token>> tokenize(std::string_view source, std::stop_token cancel = {});
};
/** Immutable-after-parse authored expression, separate from typed IR. */
struct Expression
{
    enum class Kind { literal, name, unary, binary, call } kind{};
    Token token;
    std::vector<std::unique_ptr<Expression>> children;
    std::uint16_t height{1};
};
/** Assignment/control/call syntax; FOR preserves its explicit NEXT counter token. */
struct Statement
{
    enum class Kind { compound, assign, call, conditional, loop } kind{};
    Token token, next;
    std::vector<std::unique_ptr<Expression>> expressions;
    std::vector<std::unique_ptr<Statement>> children;
};
/** Typed declaration syntax; parameter/constant expressions remain authored AST nodes. */
struct Declaration
{
    enum class Kind { input, output, parameter, constant, variable, argument } kind{};
    Token name;
    Type type{Type::real};
    std::vector<std::unique_ptr<Expression>> expressions;
};
/** Top-level routine with value arguments, fixed locals and optional Pascal-style function result. */
struct Routine
{
    Token name;
    bool function{};
    Type result{Type::real};
    std::vector<Declaration> arguments, locals;
    std::unique_ptr<Statement> body;
};
/** Whole source unit, containing no bound slots, device objects or executable host capabilities. */
struct AST
{
    Token name, rate;
    std::vector<Declaration> declarations;
    std::vector<Routine> routines;
    std::unique_ptr<Statement> body;
};
/** Recursive-descent/precedence parser with finite nesting/node budgets and first-error fail-closed behavior. */
class Parser final
{
public:
    [[nodiscard]] static CompilationResult<AST> parse(std::span<const Token> tokens, std::stop_token cancel = {});
};
/** Binding, checked folding, purity/effect/assignment analysis and bounded inline lowering into typed IR. */
class SemanticAnalyzer final
{
public:
    [[nodiscard]] static CompilationResult<std::shared_ptr<const Program>> analyze(const AST& ast, std::stop_token cancel = {});
};
/** Off-callback local compiler entry; no system compiler, native code, imports or external services. */
class Compiler final
{
public:
    [[nodiscard]] static CompilationResult<std::shared_ptr<const Program>> compile(std::string_view source, std::stop_token cancel = {});
};
}
