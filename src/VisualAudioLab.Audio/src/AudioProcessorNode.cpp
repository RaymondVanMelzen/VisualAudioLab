#include <val/audio/AudioProcessorNode.h>
#include <algorithm>

namespace val::audio
{
void InputNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
    {
        auto* destination = context.destination + channel * context.stride;
        const auto inputChannel = context.hardwareChannelMap ? context.hardwareChannelMap[channel] : channel;
        const auto* source = context.hardwareEndpointActive && context.hardware.inputs
            && inputChannel >= 0 && inputChannel < context.hardware.inputChannels
            ? context.hardware.inputs[inputChannel] : nullptr;
        if (source)
            for (int frame = 0; frame < context.frames; ++frame)
                destination[frame] = safe(source[context.hardwareOffset + frame]);
        else std::fill_n(destination, context.frames, 0.0f);
    }
}

void OutputNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
    {
        const auto outputChannel = context.hardwareChannelMap ? context.hardwareChannelMap[channel] : channel;
        if (context.hardwareEndpointActive && context.hardware.outputs
            && outputChannel >= 0 && outputChannel < context.hardware.outputChannels
            && context.hardware.outputs[outputChannel])
            std::copy_n(context.source + channel * context.stride, context.frames,
                        context.hardware.outputs[outputChannel] + context.hardwareOffset);
    }
}

}
