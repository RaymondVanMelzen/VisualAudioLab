#include <val/audio/AudioProcessorNode.h>
#include <algorithm>

namespace val::audio
{
void InputNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
    {
        auto* destination = context.destination + channel * context.stride;
        const auto* source = context.hardware.inputs && channel < context.hardware.inputChannels
            ? context.hardware.inputs[channel] : nullptr;
        if (source) std::copy_n(source, context.frames, destination);
        else std::fill_n(destination, context.frames, 0.0f);
    }
}

void OutputNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
        if (context.hardware.outputs && context.hardware.outputs[channel])
            std::copy_n(context.source + channel * context.stride, context.frames, context.hardware.outputs[channel]);
}

GainNode::GainNode(float gain, double sampleRate) noexcept
    : current_(gain), target_(gain), rampFrames_(std::max(1, static_cast<int>(sampleRate * 0.005))) {}

void GainNode::setGain(float gain) noexcept
{
    target_ = gain;
    remaining_ = rampFrames_;
    step_ = (target_ - current_) / static_cast<float>(remaining_);
}

void GainNode::process(const NodeProcessContext& context) noexcept
{
    for (int frame = 0; frame < context.frames; ++frame)
    {
        if (remaining_ > 0)
        {
            current_ += step_;
            if (--remaining_ == 0) current_ = target_;
        }
        for (int channel = 0; channel < context.channels; ++channel)
            context.destination[channel * context.stride + frame]
                = context.source[channel * context.stride + frame] * current_;
    }
}
}
