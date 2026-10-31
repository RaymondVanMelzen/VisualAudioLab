#include <val/audio/DspNodes.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace val::audio
{
namespace
{
constexpr ParameterSpec gainParameters[]{{"gain", 0, 4, 1, 0.005}};
constexpr ParameterSpec eqParameters[]{
    {"frequency", 20, 20000, 1000, 0.005}, {"decibels", -24, 24, 0, 0.005}, {"q", 0.1f, 10, 0.707f, 0.005}};
constexpr ParameterSpec delayParameters[]{
    {"seconds", 0.001f, 2, 0.25f, 0.010}, {"feedback", 0, 0.95f, 0.3f, 0.005}, {"mix", 0, 1, 0.35f, 0.005}};
constexpr ParameterSpec compressorParameters[]{
    {"thresholdDb", -60, 0, -18, 0.005}, {"ratio", 1, 20, 4, 0.005},
    {"attackSeconds", 0.0001f, 0.2f, 0.01f, 0.005}, {"releaseSeconds", 0.005f, 2, 0.1f, 0.005},
    {"makeupDb", 0, 24, 0, 0.005}};
constexpr ParameterSpec distortionParameters[]{{"drive", 1, 40, 3, 0.005}, {"mix", 0, 1, 1, 0.005}};
constexpr ParameterSpec chorusParameters[]{
    {"rateHz", 0.05f, 10, 0.8f, 0.005}, {"depthSeconds", 0, 0.015f, 0.004f, 0.010}, {"mix", 0, 1, 0.5f, 0.005}};
constexpr ParameterSpec reverbParameters[]{
    {"mix", 0, 1, 0.25f, 0.005}, {"feedback", 0.1f, 0.9f, 0.65f, 0.010}, {"damping", 0, 0.95f, 0.3f, 0.010}};
constexpr double reverbTimes[]{0.0297, 0.0371, 0.0411, 0.0437, 0.005, 0.0017};

std::size_t ringLength(double seconds, double rate) noexcept { return static_cast<std::size_t>(std::ceil(seconds * rate)) + 2; }
float delayed(const std::vector<float>& memory, std::size_t length, std::size_t position, int channel, double frames) noexcept
{
    frames = std::clamp(frames, 1.0, static_cast<double>(length - 2));
    const auto whole = static_cast<std::size_t>(frames);
    const auto fraction = static_cast<float>(frames - whole);
    const auto base = static_cast<std::size_t>(channel) * length;
    const auto first = (position + length - whole) % length;
    const auto second = (first + length - 1) % length;
    return memory[base + first] + fraction * (memory[base + second] - memory[base + first]);
}
}

GainNode::GainNode(float gain, double rate) : EffectNode(gainParameters)
{
    if (!initialParameter(0, gain)) throw std::invalid_argument("Gain must be finite and in [0,4].");
    const auto result = prepare({rate, 4096, 2});
    if (!result.succeeded) throw std::invalid_argument(result.error);
}
bool GainNode::setGain(float gain) noexcept
{
    return parameterEvent(0, gain, ParameterAction::base);
}
EqNode::EqNode() : EffectNode(eqParameters) {}
void EqNode::resetState() noexcept { z1_.fill(0); z2_.fill(0); b0_ = 1; b1_ = b2_ = a1_ = a2_ = 0; }
void EqNode::beginFrame(std::span<const float>) noexcept
{
    const auto omega = 2 * std::numbers::pi * std::min<double>(value(0), spec_.sampleRate * 0.45) / spec_.sampleRate;
    const auto a = std::pow(10.0, value(1) / 40.0);
    const auto alpha = std::sin(omega) / (2 * value(2));
    const auto cosine = std::cos(omega);
    const auto denominator = 1 + alpha / a;
    b0_ = (1 + alpha * a) / denominator;
    b1_ = -2 * cosine / denominator;
    b2_ = (1 - alpha * a) / denominator;
    a1_ = b1_;
    a2_ = (1 - alpha / a) / denominator;
}
float EqNode::sample(int channel, float input) noexcept
{
    const auto output = cleanState(b0_ * input + z1_[channel]);
    z1_[channel] = cleanState(b1_ * input - a1_ * output + z2_[channel]);
    z2_[channel] = cleanState(b2_ * input - a2_ * output);
    return static_cast<float>(output);
}

DelayNode::DelayNode() : EffectNode(delayParameters) {}
std::size_t DelayNode::requiredStorageBytes(ProcessingSpec spec) const noexcept
{ return EffectNode::requiredStorageBytes(spec) + ringLength(2, spec.sampleRate) * spec.channels * sizeof(float); }
void DelayNode::prepareState()
{
    length_ = ringLength(2, spec_.sampleRate);
    memory_.assign(length_ * spec_.channels, 0);
}
void DelayNode::resetState() noexcept { std::fill(memory_.begin(), memory_.end(), 0.0f); position_ = 0; }
float DelayNode::sample(int channel, float input) noexcept
{
    const auto wet = delayed(memory_, length_, position_, channel, value(0) * spec_.sampleRate);
    memory_[static_cast<std::size_t>(channel) * length_ + position_] = safe(input + wet * value(1));
    return input * (1 - value(2)) + wet * value(2);
}
void DelayNode::endFrame() noexcept { position_ = (position_ + 1) % length_; }

CompressorNode::CompressorNode() : EffectNode(compressorParameters) {}
void CompressorNode::resetState() noexcept { envelope_ = 0; gain_ = 1; }
void CompressorNode::beginFrame(std::span<const float> inputs) noexcept
{
    float peak{};
    for (auto input : inputs) peak = std::max(peak, std::abs(input));
    const auto seconds = peak > envelope_ ? value(2) : value(3);
    const auto coefficient = std::exp(-1.0 / (spec_.sampleRate * seconds));
    envelope_ = cleanState(coefficient * envelope_ + (1 - coefficient) * peak);
    const auto levelDb = 20 * std::log10(std::max(envelope_, 1.0e-12));
    const auto reduction = std::max(0.0, levelDb - value(0)) * (1 - 1.0 / value(1));
    gain_ = static_cast<float>(std::pow(10.0, (value(4) - reduction) / 20));
}

DistortionNode::DistortionNode() : EffectNode(distortionParameters) {}
void DistortionNode::beginFrame(std::span<const float>) noexcept { normalization_ = 1 / std::tanh(value(0)); }
float DistortionNode::sample(int, float input) noexcept
{
    const auto wet = std::tanh(input * value(0)) * normalization_;
    return input * (1 - value(1)) + wet * value(1);
}

ChorusNode::ChorusNode() : EffectNode(chorusParameters) {}
std::size_t ChorusNode::requiredStorageBytes(ProcessingSpec spec) const noexcept
{ return EffectNode::requiredStorageBytes(spec) + ringLength(0.04, spec.sampleRate) * spec.channels * sizeof(float); }
void ChorusNode::prepareState()
{
    length_ = ringLength(0.04, spec_.sampleRate);
    memory_.assign(length_ * spec_.channels, 0);
}
void ChorusNode::resetState() noexcept { std::fill(memory_.begin(), memory_.end(), 0.0f); position_ = 0; phase_ = 0; }
void ChorusNode::beginFrame(std::span<const float>) noexcept
{
    for (int channel = 0; channel < spec_.channels; ++channel)
        delay_[channel] = spec_.sampleRate * (0.020 + value(1) * std::sin(phase_ + (channel % 2) * std::numbers::pi / 2));
}
float ChorusNode::sample(int channel, float input) noexcept
{
    const auto wet = delayed(memory_, length_, position_, channel, delay_[channel]);
    memory_[static_cast<std::size_t>(channel) * length_ + position_] = input;
    return input * (1 - value(2)) + wet * value(2);
}
void ChorusNode::endFrame() noexcept
{
    position_ = (position_ + 1) % length_;
    phase_ += 2 * std::numbers::pi * value(0) / spec_.sampleRate;
    if (phase_ >= 2 * std::numbers::pi) phase_ -= 2 * std::numbers::pi;
}

ReverbNode::ReverbNode() : EffectNode(reverbParameters) {}
std::size_t ReverbNode::requiredStorageBytes(ProcessingSpec spec) const noexcept
{
    auto bytes = EffectNode::requiredStorageBytes(spec);
    for (int channel = 0; channel < spec.channels; ++channel)
        for (auto seconds : reverbTimes)
            bytes += ringLength(seconds + (channel % 2) * 0.0007, spec.sampleRate) * sizeof(float);
    return bytes;
}
void ReverbNode::prepareState()
{
    for (int channel = 0; channel < spec_.channels; ++channel)
        for (std::size_t i = 0; i < 6; ++i)
            lines_[channel][i].samples.assign(ringLength(reverbTimes[i] + (channel % 2) * 0.0007, spec_.sampleRate), 0);
}
void ReverbNode::resetState() noexcept
{
    for (auto& channel : lines_)
        for (auto& line : channel)
        {
            std::fill(line.samples.begin(), line.samples.end(), 0.0f);
            line.position = 0; line.lowpass = 0;
        }
}
float ReverbNode::sample(int channel, float input) noexcept
{
    auto& lines = lines_[channel];
    float wet{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        auto& line = lines[i];
        const auto output = line.samples[line.position];
        line.lowpass = cleanState(value(2) * line.lowpass + (1 - value(2)) * output);
        line.samples[line.position] = safe(input + value(1) * static_cast<float>(line.lowpass));
        line.position = (line.position + 1) % line.samples.size();
        wet += output * 0.25f;
    }
    for (std::size_t i = 4; i < 6; ++i)
    {
        auto& line = lines[i];
        const auto delayedValue = line.samples[line.position];
        const auto next = safe(wet + 0.5f * delayedValue);
        wet = delayedValue - 0.5f * next;
        line.samples[line.position] = next;
        line.position = (line.position + 1) % line.samples.size();
    }
    return input * (1 - value(0)) + wet * value(0);
}
}
