#pragma once

#include "AudioNode.h"
#include <val/script/Program.h>
#include <memory>

namespace val::audio
{
/**
 * Prepared verified VAL Script executor; one scalar kernel per sample frame, not per channel.
 * The immutable artifact and all scratch are owned before graph publication and retired off audio.
 * Parameters reuse AudioNode smoothing/automation/bypass. A first runtime fault latches silence
 * across all channels (including bypass) until quiescent reset/replacement; location is observable.
 */
class ScriptNode final : public AudioNode
{
public:
    explicit ScriptNode(std::shared_ptr<const script::Program> program);
    [[nodiscard]] application::Result prepare(ProcessingSpec spec) override;
    [[nodiscard]] std::size_t requiredStorageBytes(ProcessingSpec spec) const noexcept override;
    [[nodiscard]] script::RuntimeFault fault() const noexcept;
protected:
    void beginFrame(std::span<const float> inputs) noexcept override;
    float sample(int channel, float) noexcept override;
    void resetState() noexcept override;
    bool usesInput() const noexcept override { return !program_->inputs().empty(); }
    bool failed() const noexcept override { return faultCode_.load(std::memory_order_relaxed) != script::FaultCode::none; }
private:
    void record(script::RuntimeFault fault) noexcept;
    std::shared_ptr<const script::Program> program_;
    std::array<ParameterSpec, 8> definitions_{};
    std::array<script::Value, script::Program::maxSlots> scratch_{};
    std::array<float, 32> outputs_{};
    std::atomic<script::FaultCode> faultCode_{};
    std::atomic<std::uint64_t> faultLocation_{}, faultExtent_{};
    std::atomic<std::uint32_t> faultInstruction_{};
};
}
