#include <val/script/Program.h>
#include <cmath>
#include <limits>

namespace val::script
{
FaultCode evaluate(Op op, Type type, Value left, Value right, Value& result) noexcept
{
    if (op == Op::move) { result = left; return FaultCode::none; }
    if (op == Op::toReal) { result = Value::fromReal(static_cast<float>(left.integer)); return FaultCode::none; }
    if (op == Op::logicalNot) { result = Value::fromBoolean(!left.boolean); return FaultCode::none; }
    if (type == Type::boolean)
    {
        if (op != Op::equal && op != Op::notEqual) return FaultCode::invalidExecution;
        result = Value::fromBoolean(op == Op::equal ? left.boolean == right.boolean : left.boolean != right.boolean);
        return FaultCode::none;
    }
    const auto a = left.type == Type::integer ? static_cast<double>(left.integer) : static_cast<double>(left.real);
    const auto b = right.type == Type::integer ? static_cast<double>(right.integer) : static_cast<double>(right.real);
    if (op >= Op::equal && op <= Op::greaterEqual)
    {
        bool value = false;
        switch (op)
        {
            case Op::equal: value = a == b; break;
            case Op::notEqual: value = a != b; break;
            case Op::less: value = a < b; break;
            case Op::lessEqual: value = a <= b; break;
            case Op::greater: value = a > b; break;
            case Op::greaterEqual: value = a >= b; break;
            default: break;
        }
        result = Value::fromBoolean(value);
        return FaultCode::none;
    }
    if (op == Op::divide || op == Op::integerDivide || op == Op::modulo)
        if (b == 0) return FaultCode::divisionByZero;
    if (type == Type::integer)
    {
        const auto x = static_cast<std::int64_t>(left.integer), y = static_cast<std::int64_t>(right.integer);
        std::int64_t value = 0;
        switch (op)
        {
            case Op::negate: value = -x; break;
            case Op::add: value = x + y; break;
            case Op::subtract: value = x - y; break;
            case Op::multiply: value = x * y; break;
            case Op::integerDivide: value = x / y; break;
            case Op::modulo: value = x % y; break;
            default: return FaultCode::invalidExecution;
        }
        if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
            return FaultCode::integerOverflow;
        result = Value::fromInteger(static_cast<std::int32_t>(value));
    }
    else
    {
        // Convert widening operands to binary32 before arithmetic, including mixed comparisons elsewhere.
        const auto x = static_cast<float>(a), y = static_cast<float>(b);
        float value = 0;
        switch (op)
        {
            case Op::negate: value = -x; break;
            case Op::add: value = x + y; break;
            case Op::subtract: value = x - y; break;
            case Op::multiply: value = x * y; break;
            case Op::divide: value = x / y; break;
            default: return FaultCode::invalidExecution;
        }
        if (!std::isfinite(value)) return FaultCode::nonFinite;
        result = Value::fromReal(value);
    }
    return FaultCode::none;
}
std::size_t Program::storageBytes() const noexcept
{
    std::size_t bytes = sizeof(*this) + name_.capacity() + code_.capacity() * sizeof(Instruction)
        + inputs_.capacity() * sizeof(Port) + outputs_.capacity() * sizeof(Port) + parameters_.capacity() * sizeof(Parameter);
    for (const auto& port : inputs_) bytes += port.name.capacity();
    for (const auto& port : outputs_) bytes += port.name.capacity();
    for (const auto& parameter : parameters_) bytes += parameter.name.capacity();
    return bytes;
}
RuntimeFault Program::execute(std::span<Value> scratch) const noexcept
{
    if (scratch.size() < slots_) return {FaultCode::invalidExecution, {}, 0};
    for (std::size_t pc = 0, steps = 0; pc < code_.size(); ++pc, ++steps)
    {
        const auto& instruction = code_[pc];
        if (steps >= maxInstructions) return {FaultCode::invalidExecution, instruction.source, static_cast<std::uint32_t>(pc)};
        if (instruction.op == Op::jump || instruction.op == Op::jumpFalse || instruction.op == Op::jumpTrue)
        {
            const bool take = instruction.op == Op::jump
                || (instruction.op == Op::jumpTrue ? scratch[instruction.left].boolean : !scratch[instruction.left].boolean);
            if (take) pc = instruction.target - 1;
            continue;
        }
        if (instruction.op == Op::literal) scratch[instruction.destination] = instruction.literal;
        else
        {
            const auto code = evaluate(instruction.op, instruction.type, scratch[instruction.left],
                                       scratch[instruction.right], scratch[instruction.destination]);
            if (code != FaultCode::none) return {code, instruction.source, static_cast<std::uint32_t>(pc)};
        }
    }
    return {};
}
}
