#include <val/script/Compiler.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace val::script
{
namespace
{
[[noreturn]] void fail(const Token& token, std::string message) { throw Diagnostic{token.source, std::move(message)}; }
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
std::string upper(std::string_view text)
{
    std::string result(text);
    for (auto& c : result) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return result;
}
bool reserved(std::string_view name)
{
    static constexpr std::string_view words[] = {"NODE", "AT", "AUDIO", "CONTROL", "INPUT", "OUTPUT", "PARAM", "RANGE",
        "CONST", "VAR", "REAL", "INTEGER", "BOOLEAN", "FUNCTION", "PROCEDURE", "BEGIN", "END", "IF", "THEN", "ELSE",
        "FOR", "TO", "DO", "NEXT", "TRUE", "FALSE", "DIV", "MOD", "AND", "OR", "NOT"};
    return std::find(std::begin(words), std::end(words), name) != std::end(words);
}
/** Budget guard shared by all off-callback stages; cancellation never yields a partial executable. */
struct Budget
{
    std::stop_token cancel;
    std::size_t work{}, depth{};
    void tick(const Token& token)
    {
        if (++work > 200000) fail(token, "Compilation work budget exceeded.");
        if (cancel.stop_requested()) fail(token, "Compilation cancelled; previous artifact is unchanged.");
    }
    /** Limits recursive expression, statement and inlined routine traversal before consuming stack. */
    struct Depth
    {
        Budget& budget;
        Depth(Budget& owner, const Token& token) : budget(owner)
        {
            budget.tick(token);
            if (++budget.depth > 64) fail(token, "Nesting/call depth exceeds 64.");
        }
        ~Depth() { --budget.depth; }
    };
};
/** Internal bounded recursive-descent implementation; public Parser returns owned syntax only. */
class Syntax final
{
public:
    Syntax(std::span<const Token> tokens, std::stop_token cancel) : tokens_(tokens), budget_{cancel} {}
    AST unit()
    {
        expect("NODE");
        AST ast;
        ast.name = identifier(); expect("AT");
        ast.rate = take();
        if (ast.rate.text != "AUDIO" && ast.rate.text != "CONTROL") fail(ast.rate, "Expected AUDIO or CONTROL rate.");
        expect(";");
        while (peek("INPUT")) ports(ast.declarations, Declaration::Kind::input);
        while (peek("OUTPUT")) ports(ast.declarations, Declaration::Kind::output);
        while (peek("PARAM"))
        {
            take(); Declaration decl; decl.kind = Declaration::Kind::parameter; decl.name = identifier();
            expect(":"); expect("REAL"); expect("=");
            decl.expressions.push_back(expression()); expect("RANGE");
            decl.expressions.push_back(expression()); expect("..");
            decl.expressions.push_back(expression()); expect(";");
            ast.declarations.push_back(std::move(decl));
        }
        if (accept("CONST"))
            while (isIdentifier())
            {
                Declaration decl; decl.kind = Declaration::Kind::constant; decl.name = identifier();
                expect("="); decl.expressions.push_back(expression()); expect(";");
                ast.declarations.push_back(std::move(decl));
            }
        variables(ast.declarations);
        while (peek("FUNCTION") || peek("PROCEDURE"))
        {
            if (ast.routines.size() == 16) fail(current(), "At most 16 routines are supported.");
            Routine routine; routine.function = take().text == "FUNCTION"; routine.name = identifier();
            expect("(");
            if (!peek(")"))
                do
                {
                    auto names = nameList(); expect(":"); const auto scalar = type();
                    for (auto& name : names)
                    {
                        Declaration formal; formal.kind = Declaration::Kind::argument; formal.name = std::move(name); formal.type = scalar;
                        routine.arguments.push_back(std::move(formal));
                        if (routine.arguments.size() > 16) fail(routine.name, "At most 16 value arguments per routine.");
                    }
                } while (accept(";"));
            expect(")");
            if (routine.function) { expect(":"); routine.result = type(); }
            expect(";"); variables(routine.locals);
            routine.body = compound(); expect(";");
            ast.routines.push_back(std::move(routine));
        }
        ast.body = compound(); expect(".");
        if (current().kind != Token::Kind::end) fail(current(), "Unexpected content after END.");
        return ast;
    }
private:
    const Token& current() const { return tokens_[position_]; }
    bool peek(std::string_view text) const { return current().text == text; }
    Token take()
    {
        budget_.tick(current());
        auto token = current();
        if (token.kind != Token::Kind::end) ++position_;
        return token;
    }
    bool accept(std::string_view text) { if (!peek(text)) return false; take(); return true; }
    void expect(std::string_view text) { if (!accept(text)) fail(current(), "Expected '" + std::string(text) + "'."); }
    bool isIdentifier() const { return current().kind == Token::Kind::name && !reserved(current().text); }
    Token identifier() { if (!isIdentifier()) fail(current(), "Expected a non-keyword identifier."); return take(); }
    Type type()
    {
        auto token = take();
        if (token.text == "REAL") return Type::real;
        if (token.text == "INTEGER") return Type::integer;
        if (token.text == "BOOLEAN") return Type::boolean;
        fail(token, "Expected REAL, INTEGER or BOOLEAN.");
    }
    std::vector<Token> nameList()
    {
        std::vector<Token> names; names.push_back(identifier());
        while (accept(",")) names.push_back(identifier());
        return names;
    }
    void ports(std::vector<Declaration>& declarations, Declaration::Kind kind)
    {
        take(); Declaration decl; decl.kind = kind; decl.name = identifier(); decl.type = Type::real;
        expect(":"); expect("REAL"); expect(";"); declarations.push_back(std::move(decl));
    }
    void variables(std::vector<Declaration>& declarations)
    {
        if (!accept("VAR")) return;
        while (isIdentifier())
        {
            auto names = nameList(); expect(":"); const auto scalar = type(); expect(";");
            for (auto& name : names)
            {
                Declaration decl; decl.kind = Declaration::Kind::variable; decl.name = std::move(name); decl.type = scalar;
                declarations.push_back(std::move(decl));
            }
        }
    }
    template <typename T> std::unique_ptr<T> node()
    {
        if (++nodes_ > 4096) fail(current(), "AST exceeds 4096 nodes.");
        return std::make_unique<T>();
    }
    std::unique_ptr<Statement> compound()
    {
        Budget::Depth depth(budget_, current());
        auto stmt = node<Statement>(); stmt->kind = Statement::Kind::compound; stmt->token = current();
        expect("BEGIN");
        if (!peek("END"))
            do
            {
                stmt->children.push_back(statement());
                if (!accept(";")) break;
            } while (!peek("END"));
        expect("END");
        return stmt;
    }
    std::unique_ptr<Statement> statement()
    {
        Budget::Depth depth(budget_, current());
        if (peek("BEGIN")) return compound();
        auto stmt = node<Statement>(); stmt->token = current();
        if (accept("IF"))
        {
            stmt->kind = Statement::Kind::conditional; stmt->expressions.push_back(expression()); expect("THEN");
            stmt->children.push_back(statement());
            if (accept("ELSE")) stmt->children.push_back(statement());
        }
        else if (accept("FOR"))
        {
            stmt->kind = Statement::Kind::loop; stmt->token = identifier(); expect(":=");
            stmt->expressions.push_back(expression()); expect("TO");
            stmt->expressions.push_back(expression()); expect("DO");
            stmt->children.push_back(statement()); expect("NEXT"); stmt->next = identifier();
        }
        else
        {
            stmt->token = identifier();
            if (accept(":="))
            {
                stmt->kind = Statement::Kind::assign; stmt->expressions.push_back(expression());
            }
            else
            {
                stmt->kind = Statement::Kind::call; arguments(stmt->expressions);
            }
        }
        return stmt;
    }
    void arguments(std::vector<std::unique_ptr<Expression>>& expressions)
    {
        expect("(");
        if (!peek(")"))
            do
            {
                if (expressions.size() == 16) fail(current(), "At most 16 arguments.");
                expressions.push_back(expression());
            } while (accept(","));
        expect(")");
    }
    int precedence(std::string_view op) const
    {
        if (op == "OR") return 1;
        if (op == "AND") return 2;
        if (op == "=" || op == "<>" || op == "<" || op == "<=" || op == ">" || op == ">=") return 3;
        if (op == "+" || op == "-") return 4;
        if (op == "*" || op == "/" || op == "DIV" || op == "MOD") return 5;
        return 0;
    }
    std::unique_ptr<Expression> bounded(std::unique_ptr<Expression> expression)
    {
        for (const auto& child : expression->children)
            expression->height = std::max(expression->height, static_cast<std::uint16_t>(child->height + 1));
        if (expression->height > 64) fail(expression->token, "Expression tree depth exceeds 64.");
        return expression;
    }
    std::unique_ptr<Expression> expression(int minimum = 1)
    {
        Budget::Depth depth(budget_, current());
        auto left = unary();
        while (const auto rank = precedence(current().text))
        {
            if (rank < minimum) break;
            auto binary = node<Expression>(); binary->kind = Expression::Kind::binary; binary->token = take();
            binary->children.push_back(std::move(left)); binary->children.push_back(expression(rank + 1));
            left = bounded(std::move(binary));
            if (rank == 3 && precedence(current().text) == 3) fail(current(), "Chained comparisons require explicit boolean expressions.");
        }
        return left;
    }
    std::unique_ptr<Expression> unary()
    {
        Budget::Depth depth(budget_, current());
        auto expr = node<Expression>(); expr->token = current();
        if (peek("+") || peek("-") || peek("NOT"))
        {
            expr->kind = Expression::Kind::unary; take(); expr->children.push_back(unary()); return bounded(std::move(expr));
        }
        if (accept("(")) { auto grouped = expression(); expect(")"); return grouped; }
        if (current().kind == Token::Kind::integer || current().kind == Token::Kind::real || peek("TRUE") || peek("FALSE"))
        {
            expr->kind = Expression::Kind::literal; take(); return expr;
        }
        expr->token = identifier(); expr->kind = Expression::Kind::name;
        if (peek("(")) { expr->kind = Expression::Kind::call; arguments(expr->children); }
        return bounded(std::move(expr));
    }
    std::span<const Token> tokens_;
    std::size_t position_{}, nodes_{};
    Budget budget_;
};
}

CompilationResult<std::vector<Token>> Lexer::tokenize(std::string_view source, std::stop_token cancel)
{
    try
    {
        Token marker; Budget budget{cancel};
        if (source.size() > 65536) fail(marker, "Source exceeds 65536 bytes.");
        std::vector<Token> tokens; std::size_t position = 0;
        std::uint32_t line = 1, column = 1;
        auto advance = [&]
        {
            if (source[position++] == '\n') { ++line; column = 1; } else ++column;
        };
        while (position < source.size())
        {
            marker.source = {line, column, static_cast<std::uint32_t>(position), 1}; budget.tick(marker);
            const auto c = source[position];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
            if (c == '/' && position + 1 < source.size() && source[position + 1] == '/')
            {
                while (position < source.size() && source[position] != '\n') advance();
                continue;
            }
            auto token = marker; const auto began = position;
            if (letter(c))
            {
                token.kind = Token::Kind::name;
                do { advance(); } while (position < source.size() && (letter(source[position]) || digit(source[position]) || source[position] == '_'));
                if (position - began > 64) fail(token, "Identifier exceeds 64 ASCII characters.");
            }
            else if (digit(c))
            {
                token.kind = Token::Kind::integer;
                do { advance(); } while (position < source.size() && digit(source[position]));
                if (position + 1 < source.size() && source[position] == '.' && digit(source[position + 1]))
                {
                    token.kind = Token::Kind::real; advance();
                    while (position < source.size() && digit(source[position])) advance();
                }
                if (position - began > 32) fail(token, "Numeric literal exceeds 32 characters.");
            }
            else
            {
                token.kind = Token::Kind::symbol;
                if (std::string_view("+-*/=<>:;,.()").find(c) == std::string_view::npos)
                    fail(token, "Unsupported character; strings, pointers, arrays and external capabilities are not in VAL Script.");
                advance();
                if (position < source.size())
                {
                    const auto pair = source.substr(began, 2);
                    if (pair == ":=" || pair == ".." || pair == "<=" || pair == ">=" || pair == "<>") advance();
                }
            }
            token.text = upper(source.substr(began, position - began));
            token.source.length = static_cast<std::uint32_t>(position - began);
            tokens.push_back(std::move(token));
            if (tokens.size() > 16384) fail(marker, "Token budget exceeds 16384.");
        }
        tokens.push_back({Token::Kind::end, "", {line, column, static_cast<std::uint32_t>(position), 0}});
        return {std::move(tokens), {}};
    }
    catch (Diagnostic& error) { return {{}, {std::move(error)}}; }
}
CompilationResult<AST> Parser::parse(std::span<const Token> tokens, std::stop_token cancel)
{
    try
    {
        if (tokens.empty() || tokens.size() > 16385 || tokens.back().kind != Token::Kind::end)
            throw Diagnostic{{}, "Parser requires a bounded EOF-terminated token stream."};
        return {Syntax(tokens, cancel).unit(), {}};
    }
    catch (Diagnostic& error) { return {{}, {std::move(error)}}; }
}

namespace
{
/** Bound register/constant expression; only verified reads can create one. */
struct BoundValue
{
    Type type{};
    std::uint16_t slot{};
    std::optional<Value> constant;
};
/** Binding fact; definite assignment and active-counter protection are tracked separately by slot. */
struct Symbol
{
    Declaration::Kind kind{};
    Type type{};
    std::uint16_t slot{};
    std::optional<Value> constant;
    bool result{};
};
/** Finite binder/inliner, independent of AudioNode, JUCE, device state and arbitrary host callbacks. */
class Binding final
{
public:
    Binding(const AST& ast, std::stop_token cancel) : ast_(ast), budget_{cancel} {}
    void build()
    {
        if (ast_.rate.text != "AUDIO") fail(ast_.rate, "Only AUDIO rate is executable in version 1; CONTROL ticks/adapters are not implemented.");
        if (ast_.declarations.size() > 128 || ast_.routines.size() > 16) fail(ast_.name, "Declaration/routine budget exceeded (128/16).");
        for (const auto& decl : ast_.declarations) declareGlobal(decl);
        if (outputs.empty()) fail(ast_.name, "At least one output must be declared and assigned.");
        if (!inputs.empty() && inputs.size() != outputs.size())
            fail(ast_.name, "Version 1 bus integration requires zero inputs or one input per output channel.");
        for (std::size_t i = 0; i < ast_.routines.size(); ++i)
        {
            const auto& routine = ast_.routines[i];
            if (globals_.contains(routine.name.text) || !routines_.emplace(routine.name.text, i).second
                || routine.name.text == ast_.name.text || reserved(routine.name.text))
                fail(routine.name, "Duplicate or ambiguous routine name.");
        }
        const auto globalSlots = next_;
        // Validate unused routines too. Call sites are re-bound with their actual assignment/counter facts.
        for (std::size_t i = 0; i < ast_.routines.size(); ++i)
        {
            const auto& routine = ast_.routines[i];
            assigned_.clear();
            for (const auto& [name, symbol] : globals_)
            {
                (void)name;
                if (!symbol.constant) assigned_.insert(symbol.slot);
            }
            locals_.clear(); next_ = globalSlots;
            setupRoutine(routine, i);
            statement(body(routine.body, routine.name));
            if (routine.function && !assigned_.contains(resultSlot_)) fail(routine.name, "Function result is not assigned on every path.");
            code.clear(); locals_.clear(); next_ = globalSlots;
        }
        assigned_.clear(); function_ = false; visibility_ = ast_.routines.size(); locals_.clear();
        for (const auto& [name, symbol] : globals_)
        {
            (void)name;
            if (symbol.kind == Declaration::Kind::input || symbol.kind == Declaration::Kind::parameter) assigned_.insert(symbol.slot);
        }
        statement(body(ast_.body, ast_.name));
        for (const auto& output : outputs) if (!assigned_.contains(output.slot))
            fail(ast_.name, "Output '" + output.name + "' is not definitely assigned on every path.");
    }
    std::vector<Port> inputs, outputs;
    std::vector<Parameter> parameters;
    std::vector<Instruction> code;
    std::size_t highWater{};
private:
    const Statement& body(const std::unique_ptr<Statement>& pointer, const Token& token)
    { if (!pointer) fail(token, "Malformed AST: missing statement."); return *pointer; }
    const Expression& expressionAt(const std::vector<std::unique_ptr<Expression>>& expressions, std::size_t index, const Token& token)
    {
        if (index >= expressions.size() || !expressions[index]) fail(token, "Malformed AST: missing expression.");
        return *expressions[index];
    }
    std::uint16_t allocate(const Token& token)
    {
        if (next_ >= Program::maxSlots) fail(token, "Expanded scratch exceeds 256 scalar slots.");
        const auto index = static_cast<std::uint16_t>(next_++);
        assigned_.erase(index);
        highWater = std::max(highWater, next_); return index;
    }
    std::size_t emit(Instruction instruction)
    {
        if (code.size() >= Program::maxInstructions) throw Diagnostic{instruction.source, "Expanded IR exceeds 1024 instructions per frame."};
        budget_.tick({Token::Kind::end, "", instruction.source});
        code.push_back(instruction); return code.size() - 1;
    }
    BoundValue materialize(BoundValue value, const Token& token)
    {
        if (!value.constant) return value;
        value.slot = allocate(token);
        Instruction op; op.op = Op::literal; op.destination = value.slot; op.literal = *value.constant;
        op.type = value.type; op.source = token.source; emit(op);
        value.constant.reset(); return value;
    }
    BoundValue convert(BoundValue value, Type target, const Token& token)
    {
        if (value.type == target) return value;
        if (value.type != Type::integer || target != Type::real) fail(token, "Type mismatch; only INTEGER to REAL widening is implicit.");
        if (value.constant) return {Type::real, 0, Value::fromReal(static_cast<float>(value.constant->integer))};
        const auto slot = allocate(token);
        emit({Op::toReal, Type::real, slot, value.slot, 0, 0, {}, token.source});
        return {Type::real, slot, {}};
    }
    Symbol& lookup(const Token& token)
    {
        if (auto local = locals_.find(token.text); local != locals_.end()) return local->second;
        if (auto global = globals_.find(token.text); global != globals_.end()) return global->second;
        fail(token, "Unknown name '" + token.text + "'. No file, network, OS, allocation or external APIs are available.");
    }
    void checkName(const Token& name)
    {
        if (name.text.empty() || reserved(name.text) || name.text == ast_.name.text
            || globals_.contains(name.text) || locals_.contains(name.text) || routines_.contains(name.text))
            fail(name, "Duplicate/shadowed or reserved name '" + name.text + "'.");
    }
    void declareGlobal(const Declaration& decl)
    {
        budget_.tick(decl.name); checkName(decl.name);
        if (decl.kind == Declaration::Kind::constant)
        {
            auto value = constant(expressionAt(decl.expressions, 0, decl.name));
            globals_.emplace(decl.name.text, Symbol{decl.kind, value.type, 0, value});
            return;
        }
        const auto slot = allocate(decl.name);
        globals_.emplace(decl.name.text, Symbol{decl.kind, decl.type, slot, {}});
        if (decl.kind == Declaration::Kind::input || decl.kind == Declaration::Kind::output || decl.kind == Declaration::Kind::parameter)
            if (decl.type != Type::real) fail(decl.name, "Version 1 interfaces are REAL scalar channels/parameters.");
        switch (decl.kind)
        {
            case Declaration::Kind::input:
                if (inputs.size() == 32) fail(decl.name, "At most 32 input channels.");
                inputs.push_back({decl.name.text, slot, decl.name.source}); break;
            case Declaration::Kind::output:
                if (outputs.size() == 32) fail(decl.name, "At most 32 output channels.");
                outputs.push_back({decl.name.text, slot, decl.name.source}); break;
            case Declaration::Kind::parameter:
            {
                if (parameters.size() == 8) fail(decl.name, "At most eight script parameters.");
                auto realConstant = [&](std::size_t i)
                {
                    auto value = constant(expressionAt(decl.expressions, i, decl.name));
                    if (value.type == Type::boolean) fail(decl.name, "Parameter bounds/default require numeric constants.");
                    return value.type == Type::real ? value.real : static_cast<float>(value.integer);
                };
                const auto initial = realConstant(0), low = realConstant(1), high = realConstant(2);
                if (low > high || initial < low || initial > high) fail(decl.name, "Parameter default must lie within its ordered inclusive RANGE.");
                parameters.push_back({decl.name.text, slot, initial, low, high, 0.005});
                break;
            }
            case Declaration::Kind::variable: break;
            default: fail(decl.name, "Unsupported node declaration kind.");
        }
    }
    Value constant(const Expression& expression)
    {
        const auto previous = constantsOnly_; constantsOnly_ = true;
        const auto result = expr(expression);
        constantsOnly_ = previous;
        if (!result.constant) fail(expression.token, "Compile-time constant required.");
        return *result.constant;
    }
    BoundValue operation(Op op, Type operandType, Type resultType, BoundValue left, BoundValue right, const Token& token)
    {
        if (left.constant && right.constant)
        {
            Value result;
            const auto fault = evaluate(op, operandType, *left.constant, *right.constant, result);
            if (fault != FaultCode::none) fail(token, "Invalid constant arithmetic: division by zero, integer overflow or non-finite REAL.");
            return {resultType, 0, result};
        }
        left = materialize(left, token); right = materialize(right, token);
        const auto slot = allocate(token);
        emit({op, operandType, slot, left.slot, right.slot, 0, {}, token.source});
        return {resultType, slot, {}};
    }
    BoundValue expr(const Expression& expression)
    {
        Budget::Depth depth(budget_, expression.token);
        const auto& token = expression.token;
        if (expression.kind == Expression::Kind::literal)
        {
            if (token.text == "TRUE" || token.text == "FALSE") return {Type::boolean, 0, Value::fromBoolean(token.text == "TRUE")};
            if (token.kind == Token::Kind::integer)
            {
                std::int64_t value{};
                const auto parsed = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.text.data() + token.text.size()
                    || value < 0 || value > std::numeric_limits<std::int32_t>::max()) fail(token, "INTEGER literal exceeds signed 32-bit range.");
                return {Type::integer, 0, Value::fromInteger(static_cast<std::int32_t>(value))};
            }
            float value{};
            const auto parsed = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != token.text.data() + token.text.size() || !std::isfinite(value))
                fail(token, "REAL literal must be representable as finite binary32.");
            return {Type::real, 0, Value::fromReal(value)};
        }
        if (expression.kind == Expression::Kind::name)
        {
            auto& symbol = lookup(token);
            if (symbol.constant) return {symbol.type, 0, symbol.constant};
            if (constantsOnly_) fail(token, "Constants/loop bounds may read only earlier constants, not runtime values.");
            if (symbol.result) fail(token, "Function's own name is a write-only result; recursive calls are forbidden.");
            if (function_ && !locals_.contains(token.text) && (symbol.kind == Declaration::Kind::variable || symbol.kind == Declaration::Kind::output))
                fail(token, "Pure functions cannot read mutable node variables/outputs.");
            if (!assigned_.contains(symbol.slot)) fail(token, "'" + token.text + "' is read before definite assignment.");
            return {symbol.type, symbol.slot, {}};
        }
        if (expression.kind == Expression::Kind::call)
        {
            if (constantsOnly_) fail(token, "Constant expressions cannot call routines.");
            return call(token, expression.children, true);
        }
        const auto& first = expressionAt(expression.children, 0, token);
        if (expression.kind == Expression::Kind::unary)
        {
            if (token.text == "-" && first.kind == Expression::Kind::literal && first.token.kind == Token::Kind::integer && first.token.text == "2147483648")
                return {Type::integer, 0, Value::fromInteger(std::numeric_limits<std::int32_t>::min())};
            const auto value = expr(first);
            if (token.text == "NOT")
            {
                if (value.type != Type::boolean) fail(token, "NOT requires BOOLEAN.");
                return operation(Op::logicalNot, Type::boolean, Type::boolean, value, {Type::boolean, 0, Value::fromBoolean(false)}, token);
            }
            if (value.type == Type::boolean) fail(token, "Unary sign requires a numeric value.");
            if (token.text == "+") return value;
            if (token.text != "-") fail(token, "Unsupported unary operator.");
            return operation(Op::negate, value.type, value.type, value, {value.type, 0, Value{}}, token);
        }
        if (expression.kind != Expression::Kind::binary) fail(token, "Malformed AST expression.");
        auto left = expr(first);
        if (token.text == "AND" || token.text == "OR")
        {
            if (left.type != Type::boolean) fail(token, "AND/OR operands must be BOOLEAN.");
            if (constantsOnly_)
            {
                const auto right = expr(expressionAt(expression.children, 1, token));
                if (right.type != Type::boolean || !right.constant || !left.constant) fail(token, "BOOLEAN constants required.");
                return {Type::boolean, 0, Value::fromBoolean(token.text == "AND"
                    ? left.constant->boolean && right.constant->boolean : left.constant->boolean || right.constant->boolean)};
            }
            left = materialize(left, token); const auto result = allocate(token);
            emit({Op::move, Type::boolean, result, left.slot, 0, 0, {}, token.source});
            const auto branch = emit({token.text == "AND" ? Op::jumpFalse : Op::jumpTrue, Type::boolean, 0, left.slot, 0, 0, {}, token.source});
            auto right = expr(expressionAt(expression.children, 1, token));
            if (right.type != Type::boolean) fail(token, "AND/OR operands must be BOOLEAN.");
            right = materialize(right, token);
            emit({Op::move, Type::boolean, result, right.slot, 0, 0, {}, token.source});
            code[branch].target = static_cast<std::uint32_t>(code.size());
            return {Type::boolean, result, {}};
        }
        auto right = expr(expressionAt(expression.children, 1, token));
        const bool comparison = token.text == "=" || token.text == "<>" || token.text == "<" || token.text == "<="
            || token.text == ">" || token.text == ">=";
        if (left.type == Type::boolean || right.type == Type::boolean)
        {
            if (left.type != right.type || (token.text != "=" && token.text != "<>")) fail(token, "BOOLEAN supports only BOOLEAN equality/inequality.");
        }
        else
        {
            if ((token.text == "DIV" || token.text == "MOD") && (left.type != Type::integer || right.type != Type::integer))
                fail(token, "DIV/MOD require INTEGER operands.");
            if (left.type == Type::real || right.type == Type::real || token.text == "/")
            {
                left = convert(left, Type::real, token); right = convert(right, Type::real, token);
            }
        }
        static const std::map<std::string, Op> operations{{"+", Op::add}, {"-", Op::subtract}, {"*", Op::multiply}, {"/", Op::divide},
            {"DIV", Op::integerDivide}, {"MOD", Op::modulo}, {"=", Op::equal}, {"<>", Op::notEqual}, {"<", Op::less},
            {"<=", Op::lessEqual}, {">", Op::greater}, {">=", Op::greaterEqual}};
        const auto found = operations.find(token.text);
        if (found == operations.end()) fail(token, "Unsupported binary operator.");
        return operation(found->second, left.type, comparison ? Type::boolean : left.type, left, right, token);
    }
    void setupRoutine(const Routine& routine, std::size_t index)
    {
        if (routine.arguments.size() > 16 || routine.locals.size() > 128) fail(routine.name, "Routine declaration budget exceeded.");
        function_ = routine.function; visibility_ = index;
        for (const auto& formal : routine.arguments)
        {
            checkName(formal.name); const auto slot = allocate(formal.name);
            locals_.emplace(formal.name.text, Symbol{Declaration::Kind::argument, formal.type, slot, {}});
            assigned_.insert(slot);
        }
        for (const auto& local : routine.locals)
        {
            checkName(local.name); const auto slot = allocate(local.name);
            locals_.emplace(local.name.text, Symbol{Declaration::Kind::variable, local.type, slot, {}});
        }
        if (routine.function)
        {
            resultSlot_ = allocate(routine.name);
            locals_.emplace(routine.name.text, Symbol{Declaration::Kind::variable, routine.result, resultSlot_, {}, true});
        }
    }
    BoundValue call(const Token& name, const std::vector<std::unique_ptr<Expression>>& arguments, bool expectFunction)
    {
        Budget::Depth depth(budget_, name);
        const auto found = routines_.find(name.text);
        if (found == routines_.end() || found->second >= visibility_)
            fail(name, "Unknown/recursive/forward routine '" + name.text + "'; external APIs are forbidden.");
        const auto& routine = ast_.routines[found->second];
        if (routine.function != expectFunction) fail(name, expectFunction ? "Procedure cannot be used as a value." : "Function result cannot be discarded as a procedure.");
        if (function_ && !routine.function) fail(name, "Pure functions cannot call procedures.");
        if (arguments.size() != routine.arguments.size()) fail(name, "Routine argument count mismatch.");
        std::vector<BoundValue> values;
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            auto value = convert(expr(expressionAt(arguments, i, name)), routine.arguments[i].type, name);
            const auto snapshot = allocate(name);
            value = materialize(value, name);
            emit({Op::move, value.type, snapshot, value.slot, 0, 0, {}, name.source});
            values.push_back({value.type, snapshot, {}});
        }
        auto savedLocals = std::move(locals_); locals_.clear();
        const auto savedFunction = function_; const auto savedVisibility = visibility_; const auto savedResult = resultSlot_;
        setupRoutine(routine, found->second);
        for (std::size_t i = 0; i < values.size(); ++i)
            emit({Op::move, values[i].type, locals_.at(routine.arguments[i].name.text).slot, values[i].slot, 0, 0, {}, name.source});
        statement(body(routine.body, name));
        const auto result = BoundValue{routine.result, resultSlot_, {}};
        if (routine.function && !assigned_.contains(resultSlot_)) fail(routine.name, "Function must assign its result on every path.");
        locals_ = std::move(savedLocals); function_ = savedFunction; visibility_ = savedVisibility; resultSlot_ = savedResult;
        return result;
    }
    void writable(const Token& name, const Symbol& symbol)
    {
        if (symbol.constant || symbol.kind == Declaration::Kind::input || symbol.kind == Declaration::Kind::parameter
            || symbol.kind == Declaration::Kind::argument || counters_.contains(symbol.slot))
            fail(name, "Target is read-only (input/parameter/constant/argument/active FOR counter).");
        if (function_ && !locals_.contains(name.text)) fail(name, "Pure functions may write only locals and their own result.");
    }
    void statement(const Statement& stmt)
    {
        Budget::Depth depth(budget_, stmt.token);
        const auto mark = next_;
        switch (stmt.kind)
        {
            case Statement::Kind::compound:
                for (const auto& child : stmt.children) statement(body(child, stmt.token));
                break;
            case Statement::Kind::assign:
            {
                const auto symbol = lookup(stmt.token); writable(stmt.token, symbol);
                auto value = materialize(convert(expr(expressionAt(stmt.expressions, 0, stmt.token)), symbol.type, stmt.token), stmt.token);
                emit({Op::move, symbol.type, symbol.slot, value.slot, 0, 0, {}, stmt.token.source});
                assigned_.insert(symbol.slot); break;
            }
            case Statement::Kind::call: (void)call(stmt.token, stmt.expressions, false); break;
            case Statement::Kind::conditional:
            {
                auto condition = expr(expressionAt(stmt.expressions, 0, stmt.token));
                if (condition.type != Type::boolean) fail(stmt.token, "IF condition must be BOOLEAN, not numeric.");
                condition = materialize(condition, stmt.token);
                const auto branch = emit({Op::jumpFalse, Type::boolean, 0, condition.slot, 0, 0, {}, stmt.token.source});
                const auto before = assigned_;
                if (stmt.children.empty()) fail(stmt.token, "Malformed IF body.");
                statement(body(stmt.children[0], stmt.token));
                const auto yes = assigned_;
                const auto done = emit({Op::jump, Type::boolean, 0, 0, 0, 0, {}, stmt.token.source});
                code[branch].target = static_cast<std::uint32_t>(code.size());
                assigned_ = before;
                if (stmt.children.size() > 1) statement(body(stmt.children[1], stmt.token));
                std::erase_if(assigned_, [&yes](auto slot) { return !yes.contains(slot); });
                code[done].target = static_cast<std::uint32_t>(code.size());
                break;
            }
            case Statement::Kind::loop:
            {
                const auto counter = lookup(stmt.token); writable(stmt.token, counter);
                if (counter.type != Type::integer || counter.kind != Declaration::Kind::variable) fail(stmt.token, "FOR counter must be a writable declared INTEGER variable.");
                if (stmt.next.text != stmt.token.text) fail(stmt.next, "NEXT must name its matching FOR counter.");
                const auto low = constant(expressionAt(stmt.expressions, 0, stmt.token)), high = constant(expressionAt(stmt.expressions, 1, stmt.token));
                if (low.type != Type::integer || high.type != Type::integer) fail(stmt.token, "FOR bounds must be compile-time INTEGER constants.");
                const auto trips = std::max<std::int64_t>(0, static_cast<std::int64_t>(high.integer) - low.integer + 1);
                if (trips > 64) fail(stmt.token, "FOR exceeds 64 iterations; expanded IR must also fit its instruction budget.");
                if (stmt.children.size() != 1) fail(stmt.token, "Malformed FOR body.");
                const auto before = assigned_;
                const auto previousCode = code.size();
                counters_.insert(counter.slot); assigned_.insert(counter.slot);
                // Zero-trip bodies still receive semantic checks, but no executable/effects are retained.
                for (std::int64_t iteration = 0; iteration < std::max<std::int64_t>(1, trips); ++iteration)
                {
                    Instruction op; op.op = Op::literal; op.type = Type::integer; op.destination = counter.slot;
                    op.literal = Value::fromInteger(static_cast<std::int32_t>(static_cast<std::int64_t>(low.integer) + iteration));
                    op.source = stmt.token.source; emit(op);
                    statement(body(stmt.children[0], stmt.token));
                }
                if (!trips) { assigned_ = before; code.resize(previousCode); }
                counters_.erase(counter.slot); assigned_.erase(counter.slot);
                break;
            }
        }
        // Only scratch slots allocated by this statement can expire; global/caller locals stay valid.
        std::erase_if(assigned_, [mark](auto slot) { return slot >= mark; });
        next_ = mark;
    }
    const AST& ast_;
    Budget budget_;
    std::map<std::string, Symbol> globals_, locals_;
    std::map<std::string, std::size_t> routines_;
    std::set<std::uint16_t> assigned_, counters_;
    std::size_t next_{}, visibility_{};
    std::uint16_t resultSlot_{};
    bool constantsOnly_{}, function_{};
};
}

CompilationResult<std::shared_ptr<const Program>> SemanticAnalyzer::analyze(const AST& ast, std::stop_token cancel)
{
    try
    {
        Binding binding(ast, cancel); binding.build();
        for (std::size_t pc = 0; pc < binding.code.size(); ++pc)
        {
            const auto& instruction = binding.code[pc];
            const bool branch = instruction.op == Op::jump || instruction.op == Op::jumpFalse || instruction.op == Op::jumpTrue;
            if (instruction.left >= binding.highWater || instruction.right >= binding.highWater || instruction.destination >= binding.highWater
                || (branch && (instruction.target <= pc || instruction.target > binding.code.size())))
                throw Diagnostic{instruction.source, "Internal IR verification failed; no executable was produced."};
        }
        auto program = std::shared_ptr<Program>(new Program);
        program->name_ = ast.name.text;
        program->inputs_ = std::move(binding.inputs); program->outputs_ = std::move(binding.outputs);
        program->parameters_ = std::move(binding.parameters); program->code_ = std::move(binding.code);
        program->slots_ = binding.highWater;
        return {std::shared_ptr<const Program>(std::move(program)), {}};
    }
    catch (Diagnostic& error) { return {{}, {std::move(error)}}; }
}
CompilationResult<std::shared_ptr<const Program>> Compiler::compile(std::string_view source, std::stop_token cancel)
{
    try
    {
        auto tokens = Lexer::tokenize(source, cancel);
        if (!tokens.value) return {{}, std::move(tokens.diagnostics)};
        auto ast = Parser::parse(*tokens.value, cancel);
        if (!ast.value) return {{}, std::move(ast.diagnostics)};
        return SemanticAnalyzer::analyze(*ast.value, cancel);
    }
    catch (const std::bad_alloc&) { return {{}, {{{}, "Insufficient memory during off-callback compilation; no artifact was published."}}}; }
}
}
