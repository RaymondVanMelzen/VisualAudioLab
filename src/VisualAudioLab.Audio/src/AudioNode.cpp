#include <val/audio/AudioNode.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <new>
#if defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace val::audio
{
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

void ParameterSmoother::prepare(double rate, double seconds) noexcept
{
    duration_ = static_cast<int>(std::clamp(rate * seconds, 0.0, 384000.0));
}
void ParameterSmoother::reset(float value) noexcept
{
    current_ = target_ = value; step_ = 0; remaining_ = 0;
}
void ParameterSmoother::target(float value) noexcept
{
    if (value == target_) return;
    target_ = value;
    remaining_ = duration_;
    if (!remaining_) { current_ = value; step_ = 0; }
    else step_ = (value - current_) / static_cast<float>(remaining_);
}
float ParameterSmoother::next() noexcept
{
    if (remaining_ > 0)
    {
        current_ += step_;
        if (--remaining_ == 0) current_ = target_;
    }
    return current_;
}

AudioNode::AudioNode(std::span<const ParameterSpec> parameters) : definitions_(parameters)
{
    if (parameters.size() > states_.size()) throw std::invalid_argument("At most eight DSP parameters are supported.");
    for (std::size_t i = 0; i < parameters.size(); ++i) initial_[i] = parameters[i].initial;
}
application::Result AudioNode::prepare(ProcessingSpec spec)
{
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate < 8000 || spec.sampleRate > 384000
        || spec.channels < 1 || spec.channels > 32 || spec.maxFrames < 1 || spec.maxFrames > 4096
        || latencyFrames() < 0 || latencyFrames() > static_cast<int>(spec.sampleRate * 2))
        return application::Result::failure("DSP prepare requires 8-384 kHz, 1-32 channels, 1-4096 frames and bounded latency.");
    prepared_ = false;
    spec_ = spec;
    for (std::size_t i = 0; i < definitions_.size(); ++i)
    {
        const auto& parameter = definitions_[i];
        if (!parameter.name || !std::isfinite(parameter.minimum) || !std::isfinite(parameter.maximum)
            || parameter.minimum > parameter.maximum || !std::isfinite(parameter.smoothingSeconds)
            || parameter.smoothingSeconds < 0 || parameter.smoothingSeconds > 1
            || !std::isfinite(parameter.initial) || parameter.initial < parameter.minimum || parameter.initial > parameter.maximum
            || !std::isfinite(initial_[i]) || initial_[i] < parameter.minimum || initial_[i] > parameter.maximum)
            return application::Result::failure("DSP parameter definitions and initial values must be finite and bounded (smoothing 0-1 seconds).");
        if (!validTarget(static_cast<std::uint16_t>(i), initial_[i]))
            return application::Result::failure(std::string("DSP parameter ") + parameter.name + " is unsupported at this sample rate.");
    }
    try
    {
        dryDelay_.assign(latencyFrames() > 0 ? static_cast<std::size_t>(latencyFrames() + 1) * spec.channels : 0, 0.0f);
        prepareState();
    }
    catch (const std::bad_alloc&)
    {
        return application::Result::failure("Insufficient memory while preparing DSP state.");
    }
    prepared_ = true;
    reset();
    return application::Result::success();
}
std::size_t AudioNode::requiredStorageBytes(ProcessingSpec spec) const noexcept
{
    return latencyFrames() > 0 ? static_cast<std::size_t>(latencyFrames() + 1) * spec.channels * sizeof(float) : 0;
}
void AudioNode::reset() noexcept
{
    for (std::size_t i = 0; i < definitions_.size(); ++i)
    {
        states_[i] = {};
        states_[i].base = initial_[i];
        states_[i].smoother.prepare(spec_.sampleRate, definitions_[i].smoothingSeconds);
        states_[i].smoother.reset(initial_[i]);
        values_[i] = initial_[i];
    }
    wet_.prepare(spec_.sampleRate, 0.005);
    wet_.reset(initialBypass_ ? 0.0f : 1.0f);
    bypass_ = {};
    bypass_.base = initialBypass_ ? 1.0f : 0.0f;
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0f);
    dryPosition_ = 0;
    faults_.store(0, std::memory_order_relaxed);
    resetState();
}
bool AudioNode::accepts(std::uint16_t parameter, float target, ParameterAction action) const noexcept
{
    if (!std::isfinite(target) || action < ParameterAction::base || action > ParameterAction::disableAutomation) return false;
    if (parameter == bypassParameter)
        return action == ParameterAction::releaseOverride || action == ParameterAction::disableAutomation
            || (std::isfinite(target) && (target == 0 || target == 1));
    if (parameter >= definitions_.size()) return false;
    if (action == ParameterAction::releaseOverride || action == ParameterAction::disableAutomation) return true;
    const auto& definition = definitions_[parameter];
    return std::isfinite(target) && target >= definition.minimum && target <= definition.maximum && validTarget(parameter, target);
}
bool AudioNode::initialParameter(std::uint16_t parameter, float target) noexcept
{
    if (parameter >= definitions_.size() || !std::isfinite(target)
        || target < definitions_[parameter].minimum || target > definitions_[parameter].maximum) return false;
    initial_[parameter] = target;
    return true;
}
bool AudioNode::parameterEvent(std::uint16_t parameter, float target, ParameterAction action) noexcept
{
    if (!accepts(parameter, target, action)) return false;
    auto& state = parameter == bypassParameter ? bypass_ : states_[parameter];
    switch (action)
    {
        case ParameterAction::base: state.base = target; break;
        case ParameterAction::automation: state.automated = target; state.automation = true; break;
        case ParameterAction::overrideValue: state.overridden = target; state.overrideActive = true; break;
        case ParameterAction::releaseOverride: state.overrideActive = false; break;
        case ParameterAction::disableAutomation: state.automation = false; break;
    }
    if (parameter == bypassParameter) wet_.target(state.selected() == 1 ? 0.0f : 1.0f);
    else state.smoother.target(state.selected());
    return true;
}
double AudioNode::cleanState(double value) noexcept
{
    if (!std::isfinite(value) || std::abs(value) > 1.0e6)
    {
        faults_.store(faults_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return 0;
    }
    return std::abs(value) < 1.0e-20 ? 0.0 : value;
}
float AudioNode::safe(float value) noexcept
{
    if (!std::isfinite(value) || std::abs(value) > 1.0e6f)
    {
        faults_.store(faults_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return 0;
    }
    return std::abs(value) < 1.0e-20f ? 0 : value;
}
void AudioNode::process(const NodeProcessContext& context) noexcept
{
    // The prepared graph guarantees valid spans. Standalone callers have the same contract.
    if (!prepared_ || context.channels != spec_.channels || context.frames < 0 || context.frames > spec_.maxFrames
        || context.stride < context.frames || !context.destination || (usesInput() && !context.source))
    {
        faults_.store(faults_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (context.destination && context.channels > 0 && context.channels <= 32
            && context.frames > 0 && context.frames <= 4096 && context.stride >= context.frames)
            for (int channel = 0; channel < context.channels; ++channel)
                std::fill_n(context.destination + channel * context.stride, context.frames, 0.0f);
        return;
    }
#if defined(_M_X64) || defined(__SSE__)
    const auto oldControl = _mm_getcsr();
    _mm_setcsr(oldControl | 0x8040); // Restore the host's FTZ/DAZ state on return.
#endif
    std::array<float, 32> inputs{};
    const auto latency = latencyFrames();
    const auto delayLength = static_cast<std::size_t>(latency + 1);
    for (int frame = 0; frame < context.frames; ++frame)
    {
        for (std::size_t i = 0; i < definitions_.size(); ++i) values_[i] = states_[i].smoother.next();
        const auto wet = wet_.next();
        for (int channel = 0; channel < spec_.channels; ++channel)
            inputs[channel] = usesInput() ? safe(context.source[channel * context.stride + frame]) : 0;
        beginFrame({inputs.data(), static_cast<std::size_t>(spec_.channels)});
        for (int channel = 0; channel < spec_.channels; ++channel)
        {
            auto dry = inputs[channel];
            if (latency > 0)
            {
                const auto offset = static_cast<std::size_t>(channel) * delayLength;
                dryDelay_[offset + dryPosition_] = inputs[channel];
                dry = dryDelay_[offset + (dryPosition_ + 1) % delayLength];
            }
            const auto output = safe(sample(channel, inputs[channel]));
            context.destination[channel * context.stride + frame] = safe(dry * (1 - wet) + output * wet);
        }
        if (latency > 0) dryPosition_ = (dryPosition_ + 1) % delayLength;
        endFrame();
    }
#if defined(_M_X64) || defined(__SSE__)
    _mm_setcsr(oldControl);
#endif
}

void AnalyzerNode::resetState() noexcept
{
    peak_.fill(0); squares_.fill(0); frames_ = 0;
    for (auto& value : publishedPeak_) value.store(0, std::memory_order_relaxed);
    for (auto& value : publishedRms_) value.store(0, std::memory_order_relaxed);
    windows_.store(0, std::memory_order_relaxed);
}
float AnalyzerNode::sample(int channel, float input) noexcept
{
    peak_[channel] = std::max(peak_[channel], std::abs(input));
    squares_[channel] += static_cast<double>(input) * input;
    return input;
}
void AnalyzerNode::endFrame() noexcept
{
    if (++frames_ < 256) return;
    for (int channel = 0; channel < spec_.channels; ++channel)
    {
        publishedPeak_[channel].store(std::bit_cast<std::uint32_t>(peak_[channel]), std::memory_order_relaxed);
        publishedRms_[channel].store(std::bit_cast<std::uint32_t>(static_cast<float>(std::sqrt(squares_[channel] / 256))),
                                     std::memory_order_relaxed);
    }
    windows_.store(windows_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    peak_.fill(0); squares_.fill(0); frames_ = 0;
}
AnalyzerSnapshot AnalyzerNode::snapshot() const noexcept
{
    AnalyzerSnapshot result;
    result.windows = windows_.load(std::memory_order_acquire);
    for (std::size_t channel = 0; channel < result.peak.size(); ++channel)
    {
        result.peak[channel] = std::bit_cast<float>(publishedPeak_[channel].load(std::memory_order_relaxed));
        result.rms[channel] = std::bit_cast<float>(publishedRms_[channel].load(std::memory_order_relaxed));
    }
    return result;
}
}
