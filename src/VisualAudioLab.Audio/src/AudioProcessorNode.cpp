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
        if (source)
            for (int frame = 0; frame < context.frames; ++frame)
                destination[frame] = safe(source[context.hardwareOffset + frame]);
        else std::fill_n(destination, context.frames, 0.0f);
    }
}

void OutputNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
        if (context.hardware.outputs && context.hardware.outputs[channel])
            std::copy_n(context.source + channel * context.stride, context.frames,
                        context.hardware.outputs[channel] + context.hardwareOffset);
}

}
