#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace val::script
{
/** One-based source location plus byte extent, retained in diagnostics and executable instructions. */
struct SourceSpan
{
    std::uint32_t line{1}, column{1}, offset{}, length{};
};
/** Language version 1 scalar domains; REAL is finite IEEE binary32, INTEGER checked signed 32-bit. */
enum class Type { real, integer, boolean };
/** Explicit scalar representation shared by checked constant evaluation and the bounded executor. */
struct Value
{
    Type type{Type::integer};
    float real{};
    std::int32_t integer{};
    bool boolean{};
    static Value fromReal(float value) noexcept { return {Type::real, value}; }
    static Value fromInteger(std::int32_t value) noexcept { return {Type::integer, 0, value}; }
    static Value fromBoolean(bool value) noexcept { return {Type::boolean, 0, 0, value}; }
};
/** No host calls, pointers, heap operations or backward jumps exist in this IR vocabulary. */
enum class Op
{
    literal, move, toReal, negate, logicalNot, add, subtract, multiply, divide, integerDivide,
    modulo, equal, notEqual, less, lessEqual, greater, greaterEqual, jumpFalse, jumpTrue, jump
};
/** Typed register operation with source mapping; all branch targets in verified artifacts are forward. */
struct Instruction
{
    Op op{};
    Type type{Type::integer};
    std::uint16_t destination{}, left{}, right{};
    std::uint32_t target{};
    Value literal;
    SourceSpan source;
};
/** Bounded runtime failure; caller must latch a failed state and silence, not continue partial results. */
enum class FaultCode : std::uint32_t { none, integerOverflow, divisionByZero, nonFinite, invalidExecution, outputRange };
/** Allocation-free first fault data, meaningful only when code is not none. */
struct RuntimeFault
{
    /** Node-level guards point to a declaration rather than an executable instruction. */
    static constexpr std::uint32_t noInstruction = std::numeric_limits<std::uint32_t>::max();
    FaultCode code{FaultCode::none};
    SourceSpan source;
    std::uint32_t instruction{};
};
/** Immutable scalar port identity; declaration order maps to explicit bus channel order. */
struct Port
{
    std::string name;
    std::uint16_t slot{};
    SourceSpan source;
};
/** REAL parameter metadata; index follows declaration order and uses the existing node parameter layers. */
struct Parameter
{
    std::string name;
    std::uint16_t slot{};
    float initial{}, minimum{}, maximum{};
    double smoothingSeconds{0.005};
};
class SemanticAnalyzer;
/**
 * Compiler-only constructed, immutable verified instruction plan, language/IR version 1.
 * No deserialization/public mutation path exists. Share across prepared nodes; reclaim off callback.
 * execute is bounded by instruction count and uses only caller-preallocated scalar scratch.
 */
class Program final
{
public:
    static constexpr std::size_t maxInstructions = 1024, maxSlots = 256;
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::span<const Port> inputs() const noexcept { return inputs_; }
    [[nodiscard]] std::span<const Port> outputs() const noexcept { return outputs_; }
    [[nodiscard]] std::span<const Parameter> parameters() const noexcept { return parameters_; }
    [[nodiscard]] std::span<const Instruction> instructions() const noexcept { return code_; }
    [[nodiscard]] std::size_t slots() const noexcept { return slots_; }
    [[nodiscard]] std::size_t storageBytes() const noexcept;
    /** Inputs/parameters must be assigned to their declared slots before each execution. */
    [[nodiscard]] RuntimeFault execute(std::span<Value> scratch) const noexcept;
private:
    friend class SemanticAnalyzer;
    Program() = default;
    std::string name_;
    std::vector<Port> inputs_, outputs_;
    std::vector<Parameter> parameters_;
    std::vector<Instruction> code_;
    std::size_t slots_{};
};
/** Checked primitive semantics; no undefined integer arithmetic or non-finite successful result. */
[[nodiscard]] FaultCode evaluate(Op operation, Type type, Value left, Value right, Value& result) noexcept;
}
