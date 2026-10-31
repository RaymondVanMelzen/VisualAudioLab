#pragma once

#include <val/audio/DspNodes.h>

namespace val::audio
{
/** Source compatibility for the original engine API; AudioNode is the single processor hierarchy. */
using AudioProcessorNode = AudioNode;

/** Copies corresponding hardware input channels; absent/null inputs yield silence. */
class InputNode final : public AudioProcessorNode
{
public:
    void process(const NodeProcessContext& context) noexcept override;
private:
    float sample(int, float input) noexcept override { return input; }
};

/** Copies one upstream bus to the corresponding hardware outputs; no conversion or mixing. */
class OutputNode final : public AudioProcessorNode
{
public:
    void process(const NodeProcessContext& context) noexcept override;
private:
    float sample(int, float input) noexcept override { return input; }
};
}
