#include <val/audio/AudioNode.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <new>
#include <string_view>
#if defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace val::audio
{
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

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

AudioNode::AudioNode(std::span<const ParameterSpec> parameters)
{
    defineParameters(parameters);
}
void AudioNode::defineParameters(std::span<const ParameterSpec> parameters)
{
    if (parameters.size() > states_.size()) throw std::invalid_argument("At most twenty-four DSP parameters are supported.");
    definitions_ = parameters;
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
        controls_.clear();
        controls_.reserve(initialControls_.size());
        for (const auto& binding : initialControls_)
        {
            ControlRuntime runtime;
            runtime.binding = binding;
            if (binding.source == ParameterControlSource::script)
                runtime.scratch = std::make_unique<script::Value[]>(script::Program::maxSlots);
            controls_.push_back(std::move(runtime));
        }
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
    auto bytes = latencyFrames() > 0
        ? static_cast<std::size_t>(latencyFrames() + 1) * spec.channels * sizeof(float) : 0;
    bytes += initialControls_.size() * sizeof(ControlRuntime);
    for (const auto& control : initialControls_)
        if (control.source == ParameterControlSource::script)
            bytes += script::Program::maxSlots * sizeof(script::Value)
                + (control.program ? control.program->storageBytes() : 0);
    return bytes;
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
    processedFrames_.store(0, std::memory_order_relaxed);
    controlFailed_.store(false, std::memory_order_relaxed);
    for (auto& control : controls_) { control.phase = 0; control.frame = 0; }
    resetState();
}
bool AudioNode::controlRangeFits(std::uint16_t parameter, float base) const noexcept
{
    for (const auto& control : initialControls_)
        if (control.parameter == parameter)
        {
            const auto first = base + control.offset + control.depth * control.sourceMinimum;
            const auto last = base + control.offset + control.depth * control.sourceMaximum;
            const auto low = std::min(first, last), high = std::max(first, last);
            return std::isfinite(low) && std::isfinite(high)
                && low >= definitions_[parameter].minimum && high <= definitions_[parameter].maximum;
        }
    return true;
}
bool AudioNode::initialControl(ParameterControlBinding binding)
{
    if ((prepared_ && processedFrames_.load(std::memory_order_relaxed) != 0)
        || binding.parameter >= definitions_.size()
        || !std::isfinite(binding.offset) || !std::isfinite(binding.depth)
        || !std::isfinite(binding.sourceMinimum) || !std::isfinite(binding.sourceMaximum)
        || binding.sourceMinimum >= binding.sourceMaximum
        || definitions_[binding.parameter].smoothingSeconds <= 0)
        return false;
    if (std::any_of(initialControls_.begin(), initialControls_.end(), [&](const auto& existing)
        { return existing.parameter == binding.parameter; }))
        return false;
    switch (binding.source)
    {
        case ParameterControlSource::lfo:
            if (!std::isfinite(binding.rateHz) || binding.rateHz < 0.01f || binding.rateHz > 20
                || binding.sourceMinimum != -1 || binding.sourceMaximum != 1 || binding.program)
                return false;
            break;
        case ParameterControlSource::envelope:
            if (!std::isfinite(binding.attackSeconds) || !std::isfinite(binding.holdSeconds)
                || !std::isfinite(binding.releaseSeconds) || binding.attackSeconds < 0
                || binding.holdSeconds < 0 || binding.releaseSeconds <= 0
                || binding.attackSeconds > 60 || binding.holdSeconds > 60 || binding.releaseSeconds > 60
                || binding.sourceMinimum != 0 || binding.sourceMaximum != 1 || binding.program)
                return false;
            break;
        case ParameterControlSource::script:
        {
            const auto timeName = binding.program && binding.program->inputs().size() == 1
                ? std::string_view(binding.program->inputs().front().name) : std::string_view{};
            const bool hasTimeInput = timeName.size() == 4
                && (timeName[0] == 'T' || timeName[0] == 't')
                && (timeName[1] == 'I' || timeName[1] == 'i')
                && (timeName[2] == 'M' || timeName[2] == 'm')
                && (timeName[3] == 'E' || timeName[3] == 'e');
            if (!binding.program || binding.program->inputs().size() != 1
                || binding.program->outputs().size() != 1 || !binding.program->parameters().empty()
                || !hasTimeInput)
                return false;
            break;
        }
        default: return false;
    }
    const auto first = initial_[binding.parameter] + binding.offset
        + binding.depth * binding.sourceMinimum;
    const auto last = initial_[binding.parameter] + binding.offset
        + binding.depth * binding.sourceMaximum;
    if (!std::isfinite(first) || !std::isfinite(last)
        || std::min(first, last) < definitions_[binding.parameter].minimum
        || std::max(first, last) > definitions_[binding.parameter].maximum)
        return false;
    const bool reprepare = prepared_;
    if (reprepare) prepared_ = false;
    try { initialControls_.push_back(std::move(binding)); }
    catch (const std::bad_alloc&)
    {
        if (reprepare) (void) prepare(spec_);
        return false;
    }
    if (reprepare)
    {
        const auto result = prepare(spec_);
        if (!result.succeeded)
        {
            initialControls_.pop_back();
            prepared_ = false;
            (void) prepare(spec_);
            return false;
        }
    }
    return true;
}
float AudioNode::controlSample(ControlRuntime& control) noexcept
{
    if (controlFailed_.load(std::memory_order_relaxed)) return 0;
    float source{};
    switch (control.binding.source)
    {
        case ParameterControlSource::lfo:
            source = static_cast<float>(std::sin(2.0 * std::numbers::pi * control.phase));
            control.phase += static_cast<double>(control.binding.rateHz) / spec_.sampleRate;
            control.phase -= std::floor(control.phase);
            break;
        case ParameterControlSource::envelope:
        {
            const auto time = static_cast<double>(control.frame) / spec_.sampleRate;
            const auto attack = static_cast<double>(control.binding.attackSeconds);
            const auto holdEnd = attack + control.binding.holdSeconds;
            const auto releaseEnd = holdEnd + control.binding.releaseSeconds;
            if (time < attack && attack > 0) source = static_cast<float>(time / attack);
            else if (time < holdEnd) source = 1;
            else if (time < releaseEnd) source = static_cast<float>(1.0 - (time - holdEnd) / control.binding.releaseSeconds);
            break;
        }
        case ParameterControlSource::script:
        {
            const auto& program = *control.binding.program;
            control.scratch[program.inputs().front().slot] =
                script::Value::fromReal(static_cast<float>(static_cast<double>(control.frame) / spec_.sampleRate));
            const auto result = program.execute({control.scratch.get(), program.slots()});
            const auto value = control.scratch[program.outputs().front().slot].real;
            if (result.code != script::FaultCode::none || !std::isfinite(value)
                || value < control.binding.sourceMinimum || value > control.binding.sourceMaximum)
            {
                faults_.fetch_add(1, std::memory_order_relaxed);
                controlFailed_.store(true, std::memory_order_relaxed);
            }
            else source = value;
            break;
        }
    }
    if (control.frame < std::numeric_limits<std::uint64_t>::max()) ++control.frame;
    return source;
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
    return std::isfinite(target) && target >= definition.minimum && target <= definition.maximum
        && controlRangeFits(parameter, target) && validTarget(parameter, target);
}
bool AudioNode::initialParameter(std::uint16_t parameter, float target) noexcept
{
    if (parameter >= definitions_.size() || !std::isfinite(target)
        || target < definitions_[parameter].minimum || target > definitions_[parameter].maximum
        || !controlRangeFits(parameter, target)) return false;
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
        for (auto& control : controls_)
        {
            const auto parameter = control.binding.parameter;
            values_[parameter] += control.binding.offset
                + control.binding.depth * controlSample(control);
            values_[parameter] = std::clamp(values_[parameter],
                definitions_[parameter].minimum, definitions_[parameter].maximum);
        }
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
            context.destination[channel * context.stride + frame] =
                failed() || controlFailed_.load(std::memory_order_relaxed)
                    ? 0 : safe(dry * (1 - wet) + output * wet);
        }
        if (latency > 0) dryPosition_ = (dryPosition_ + 1) % delayLength;
        endFrame();
    }
    processedFrames_.fetch_add(static_cast<std::uint64_t>(context.frames), std::memory_order_relaxed);
#if defined(_M_X64) || defined(__SSE__)
    _mm_setcsr(oldControl);
#endif
}

void AnalyzerNode::process(const NodeProcessContext& context) noexcept
{
    for (int channel = 0; channel < context.channels; ++channel)
        std::copy_n(context.source + channel * context.stride, context.frames, context.destination + channel * context.stride);
}
AnalyzerSnapshot AnalyzerNode::snapshot() const noexcept
{
    return capture_ ? capture_->meters() : AnalyzerSnapshot{};
}
}
