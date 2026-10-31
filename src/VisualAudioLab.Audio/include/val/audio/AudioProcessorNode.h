#pragma once

#include <val/application/AudioStream.h>
#include <cstdint>

namespace val::audio
{
/** Preallocated planar scratch block, with immutable upstream samples and explicit hardware spans. */
struct NodeProcessContext
{
    const float* source{};
    float* destination{};
    int channels{}, stride{}, frames{};
    const application::AudioBuffer& hardware;
};

/** Prepared node instance; construction/destruction belongs exclusively to the control thread. */
class AudioProcessorNode
{
public:
    virtual ~AudioProcessorNode() = default;
    virtual void process(const NodeProcessContext& context) noexcept = 0;
};

/** Copies corresponding hardware input channels; absent/null inputs yield silence. */
class InputNode final : public AudioProcessorNode
{
public:
    void process(const NodeProcessContext& context) noexcept override;
};

/** Copies one upstream bus to the corresponding hardware outputs; no conversion or mixing. */
class OutputNode final : public AudioProcessorNode
{
public:
    void process(const NodeProcessContext& context) noexcept override;
};

/** Linear gain in [0,4], ramped over five milliseconds for accepted live changes. */
class GainNode final : public AudioProcessorNode
{
public:
    GainNode(float gain, double sampleRate) noexcept;
    /** Audio-thread-only target update; values are validated before queueing. */
    void setGain(float gain) noexcept;
    void process(const NodeProcessContext& context) noexcept override;
private:
    float current_{}, target_{}, step_{};
    int rampFrames_{}, remaining_{};
};
}
