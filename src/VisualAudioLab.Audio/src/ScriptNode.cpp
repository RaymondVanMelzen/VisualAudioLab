#include <val/audio/ScriptNode.h>
#include <cmath>
#include <stdexcept>

namespace val::audio
{
static_assert(std::atomic<script::FaultCode>::is_always_lock_free);
ScriptNode::ScriptNode(std::shared_ptr<const script::Program> program) : program_(std::move(program))
{
    if (!program_) throw std::invalid_argument("ScriptNode requires a verified compiler artifact.");
    for (std::size_t i = 0; i < program_->parameters().size(); ++i)
    {
        const auto& parameter = program_->parameters()[i];
        definitions_[i] = {parameter.name.c_str(), parameter.minimum, parameter.maximum, parameter.initial, parameter.smoothingSeconds};
    }
    defineParameters({definitions_.data(), program_->parameters().size()});
}
application::Result ScriptNode::prepare(ProcessingSpec spec)
{
    if (program_->outputs().size() != static_cast<std::size_t>(spec.channels)
        || (!program_->inputs().empty() && program_->inputs().size() != static_cast<std::size_t>(spec.channels)))
        return application::Result::failure("Script scalar INPUT/OUTPUT declaration counts must match the prepared channel bus (or have zero inputs).");
    return AudioNode::prepare(spec);
}
std::size_t ScriptNode::requiredStorageBytes(ProcessingSpec spec) const noexcept
{
    return sizeof(*this) + program_->storageBytes() + AudioNode::requiredStorageBytes(spec);
}
void ScriptNode::resetState() noexcept
{
    scratch_ = {}; outputs_ = {};
    faultCode_.store(script::FaultCode::none, std::memory_order_release);
    faultLocation_.store(0, std::memory_order_relaxed); faultExtent_.store(0, std::memory_order_relaxed);
    faultInstruction_.store(0, std::memory_order_relaxed);
}
void ScriptNode::record(script::RuntimeFault fault) noexcept
{
    faultLocation_.store((static_cast<std::uint64_t>(fault.source.line) << 32) | fault.source.column, std::memory_order_relaxed);
    faultExtent_.store((static_cast<std::uint64_t>(fault.source.offset) << 32) | fault.source.length, std::memory_order_relaxed);
    faultInstruction_.store(fault.instruction, std::memory_order_relaxed);
    faultCode_.store(fault.code, std::memory_order_release);
    outputs_.fill(0);
}
script::RuntimeFault ScriptNode::fault() const noexcept
{
    script::RuntimeFault result;
    result.code = faultCode_.load(std::memory_order_acquire);
    if (result.code == script::FaultCode::none) return result;
    const auto location = faultLocation_.load(std::memory_order_relaxed), extent = faultExtent_.load(std::memory_order_relaxed);
    result.source = {static_cast<std::uint32_t>(location >> 32), static_cast<std::uint32_t>(location),
                     static_cast<std::uint32_t>(extent >> 32), static_cast<std::uint32_t>(extent)};
    result.instruction = faultInstruction_.load(std::memory_order_relaxed);
    return result;
}
void ScriptNode::beginFrame(std::span<const float> inputs) noexcept
{
    if (failed()) return;
    for (std::size_t i = 0; i < program_->inputs().size(); ++i)
        scratch_[program_->inputs()[i].slot] = script::Value::fromReal(inputs[i]);
    for (std::size_t i = 0; i < program_->parameters().size(); ++i)
        scratch_[program_->parameters()[i].slot] = script::Value::fromReal(value(i));
    const auto execution = program_->execute({scratch_.data(), program_->slots()});
    if (execution.code != script::FaultCode::none) { record(execution); return; }
    for (std::size_t i = 0; i < program_->outputs().size(); ++i)
    {
        const auto output = scratch_[program_->outputs()[i].slot].real;
        if (!std::isfinite(output) || std::abs(output) > 1.0e6f)
        {
            record({std::isfinite(output) ? script::FaultCode::outputRange : script::FaultCode::nonFinite,
                    program_->outputs()[i].source, script::RuntimeFault::noInstruction});
            return;
        }
        outputs_[i] = output;
    }
}
float ScriptNode::sample(int channel, float) noexcept { return outputs_[channel]; }
}
