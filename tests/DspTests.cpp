#include "TestSupport.h"
#include "AudioAllocationProbe.h"
#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

namespace
{
using namespace val::audio;
using namespace val::application;
static_assert(std::is_same_v<AudioNode, AudioProcessorNode>);
static_assert(std::is_base_of_v<EffectNode, GainNode>);

bool near(double actual, double expected, double tolerance = 1.0e-5)
{ return std::abs(actual - expected) <= tolerance; }

void process(AudioNode& node, const NodeProcessContext& context)
{
    std::uint64_t allocations{}, frees{};
    {
        allocationProbe::Scope probe;
        node.process(context);
        allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
    }
    require(allocations == 0 && frees == 0, "Every DSP callback is free of instrumented C++ allocations/deallocations.");
}
void event(AudioNode& node, std::uint16_t parameter, float value, ParameterAction action = ParameterAction::automation)
{
    bool accepted{};
    {
        allocationProbe::Scope probe;
        accepted = node.parameterEvent(parameter, value, action);
        require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0, "Parameter handoff does not allocate.");
    }
    require(accepted, "Accept valid parameter transition.");
}
void initial(AudioNode& node, std::uint16_t parameter, float value)
{ require(node.initialParameter(parameter, value), "Validate initial physical-domain parameter."); }
void prepare(AudioNode& node, double rate = 48000, int channels = 1)
{ require(node.prepare({rate, 512, channels}).succeeded, "Prepare actual processor."); }

std::vector<float> render(AudioNode& node, const std::vector<float>& input, int channels = 1, int block = 128)
{
    const auto frames = static_cast<int>(input.size()) / channels;
    std::vector<float> output(input.size(), 99);
    const AudioBuffer hardware;
    for (int offset = 0; offset < frames; offset += block)
        process(node, {input.data() + offset, output.data() + offset, channels, frames,
                       std::min(block, frames - offset), hardware});
    return output;
}
double rms(const std::vector<float>& values, std::size_t start, std::size_t end)
{
    double sum{};
    for (auto i = start; i < end; ++i) sum += static_cast<double>(values[i]) * values[i];
    return std::sqrt(sum / static_cast<double>(end - start));
}

void testGainAndLayers()
{
    GainNode gain(0.5f);
    prepare(gain);
    std::vector<float> input(512, 1);
    require(render(gain, input)[0] == 0.5f, "Original Gain processing remains exact.");
    event(gain, 0, 1);
    const auto ramp = render(gain, input, 1, 17);
    require(near(ramp[0], 0.5 + 0.5 / 240) && ramp[239] == 1, "Sample-time smoothing is independent of callbacks.");
    event(gain, 0, 0.25f, ParameterAction::base);
    require(render(gain, input).back() == 1, "Saved base changes do not replace active automation.");
    event(gain, 0, 0.75f, ParameterAction::overrideValue);
    require(render(gain, input).back() == 0.75f, "Explicit live override wins.");
    event(gain, 0, 0.4f);
    require(render(gain, input).back() == 0.75f, "Automation continues underneath override.");
    event(gain, 0, 0, ParameterAction::releaseOverride);
    require(render(gain, input).back() == 0.4f, "Release resumes latest automation target.");
    event(gain, 0, 0, ParameterAction::disableAutomation);
    require(render(gain, input).back() == 0.25f, "Disabling automation restores saved base.");
    gain.reset();
    require(render(gain, input)[0] == 0.5f, "Reset restores prepared initial values, not transient runtime layers.");
    event(gain, 0, 0.75f);
    const AudioBuffer hardware;
    float dummy{};
    process(gain, {&dummy, &dummy, 1, 1, 0, hardware});
    require(near(render(gain, input)[0], 0.5 + 0.25 / 240), "Zero frames do not advance a ramp.");
    require(!gain.parameterEvent(0, std::numeric_limits<float>::quiet_NaN(), ParameterAction::base)
            && !gain.parameterEvent(100, 1, ParameterAction::base)
            && !gain.parameterEvent(0, 5, ParameterAction::automation),
            "Malformed parameter requests are rejected explicitly.");
    prepare(gain, 96000);
    event(gain, 0, 1);
    const auto highRate = render(gain, input);
    require(highRate[479] == 1 && highRate[239] < 0.76f, "Reprepare recalculates the five-millisecond ramp at the new rate.");
    ParameterSmoother step;
    step.prepare(48000, 0); step.reset(0); step.target(1);
    require(step.next() == 1, "Explicit no-smoothing metadata permits exact target steps.");
}

void testEq()
{
    EqNode flat;
    prepare(flat);
    std::vector<float> impulse(4096);
    impulse[0] = 1;
    const auto identity = render(flat, impulse);
    require(identity[0] == 1 && rms(identity, 1, identity.size()) < 1.0e-6, "Zero-dB EQ is identity.");
    prepare(flat, 8000);
    require(!flat.parameterEvent(0, 10000, ParameterAction::automation),
            "EQ rejects frequencies beyond 45 percent of its negotiated sample rate rather than silently clamping.");
    initial(flat, 0, 10000);
    require(!flat.prepare({8000, 512, 1}).succeeded, "Initial settings are checked against the new negotiated rate.");
    prepare(flat, 96000);
    for (double rate : {44100.0, 96000.0})
    {
        EqNode peak;
        initial(peak, 0, 1000); initial(peak, 1, 6); initial(peak, 2, 1);
        prepare(peak, rate, 2);
        const auto count = static_cast<int>(rate / 2);
        std::vector<float> sine(static_cast<std::size_t>(count) * 2);
        for (int i = 0; i < count; ++i) sine[i] = static_cast<float>(0.1 * std::sin(2 * std::numbers::pi * 1000 * i / rate));
        const auto filtered = render(peak, sine, 2, 127);
        require(near(rms(filtered, count / 2, count) / rms(sine, count / 2, count), std::pow(10.0, 6.0 / 20), 0.005),
                "Peaking EQ gives declared +6 dB at center frequency.");
        require(rms(filtered, count, filtered.size()) == 0, "EQ history is independent per channel.");
        peak.reset();
        const auto first = render(peak, sine, 2, 127);
        peak.reset();
        const auto partitioned = render(peak, sine, 2, 61);
        require(first == partitioned, "Filter state is invariant to callback partitioning.");
    }
}

void testDelay()
{
    DelayNode delay;
    initial(delay, 0, 0.010f); initial(delay, 1, 0.5f); initial(delay, 2, 1);
    for (double rate : {48000.0, 96000.0})
    {
        prepare(delay, rate, 2);
        const auto period = static_cast<std::size_t>(rate * 0.010);
        const auto frames = period * 4;
        std::vector<float> impulse(frames * 2);
        impulse[0] = 1;
        const auto output = render(delay, impulse, 2, 97);
        require(near(output[period], 1, 1.0e-4) && near(output[2 * period], 0.5, 1.0e-4)
                && near(output[3 * period], 0.25, 1.0e-4), "Delay time/feedback produce expected physical-time echoes.");
        require(rms(output, frames, output.size()) == 0, "Delay has independent stereo histories.");
        delay.reset();
        require(rms(render(delay, std::vector<float>(frames * 2), 2), 0, frames * 2) == 0, "Reset removes delay tails.");
    }
    prepare(delay, 8000);
    initial(delay, 0, 2); initial(delay, 1, 0); prepare(delay, 8000);
    std::vector<float> maximum(16002); maximum[0] = 1;
    require(near(render(delay, maximum)[16000], 1), "Maximum declared delay is actually available without callback growth.");
}

void testCompressor()
{
    CompressorNode compressor;
    initial(compressor, 0, -20); initial(compressor, 1, 4);
    initial(compressor, 2, 0.001f); initial(compressor, 3, 0.05f);
    prepare(compressor, 48000, 2);
    std::vector<float> signal(48000, 1); // 24000 frames, right channel is quieter.
    std::fill(signal.begin() + 24000, signal.end(), 0.25f);
    const auto output = render(compressor, signal, 2);
    const auto expected = std::pow(10.0, -15.0 / 20);
    require(near(output[23999], expected, 1.0e-4) && near(output.back() / output[23999], 0.25, 1.0e-6),
            "Linked compressor gives 4:1 above threshold and preserves stereo balance.");
    require(output[0] > output[1000], "Attack envelope is time-based, not an instant hard clamp.");
    initial(compressor, 1, 1); prepare(compressor, 96000);
    require(render(compressor, std::vector<float>(2048, 0.5f)).back() == 0.5f, "Unity ratio and zero makeup give identity.");
}

void testDistortion()
{
    DistortionNode distortion;
    initial(distortion, 0, 5); prepare(distortion);
    const std::vector<float> input{0, 0.25f, -0.25f, 1, -1, 100, -100};
    const auto output = render(distortion, input);
    require(output[0] == 0 && near(output[1], std::tanh(1.25) / std::tanh(5.0))
            && output[1] == -output[2] && near(output[3], 1)
            && std::abs(output[5]) < 1.001, "Waveshaper is real, odd-symmetric, normalized and bounded.");
    initial(distortion, 1, 0); prepare(distortion);
    require(render(distortion, input) == input, "Dry mix is identity.");
}

void testChorus()
{
    ChorusNode chorus;
    initial(chorus, 1, 0); initial(chorus, 2, 1); prepare(chorus, 48000, 2);
    std::vector<float> impulse(4096);
    impulse[0] = impulse[2048] = 1;
    const auto fixed = render(chorus, impulse, 2, 67);
    require(near(fixed[960], 1) && near(fixed[2048 + 960], 1), "Zero-depth chorus is the documented 20ms delay.");
    initial(chorus, 1, 0.010f); prepare(chorus, 48000, 2);
    const auto moving = render(chorus, impulse, 2, 67);
    require(moving != fixed && rms(moving, 2048, 4096) > 0, "Chorus modulation changes delay and preserves independent stereo output.");
    chorus.reset();
    const auto partitioned = render(chorus, impulse, 2, 113);
    require(moving == partitioned, "LFO advances by samples, not blocks.");
    prepare(chorus, 96000);
    initial(chorus, 1, 0); prepare(chorus, 96000);
    std::vector<float> high(4096); high[0] = 1;
    require(near(render(chorus, high)[1920], 1), "Chorus prepare sizes/retimes delay for a sample-rate change.");
}

void testReverb()
{
    ReverbNode reverb;
    initial(reverb, 0, 1); prepare(reverb, 48000, 2);
    constexpr int frames = 48000;
    std::vector<float> impulse(frames * 2);
    impulse[0] = 1;
    const auto output = render(reverb, impulse, 2, 83);
    require(rms(output, 0, 1000) == 0 && rms(output, 1000, 10000) > 0.001
            && rms(output, 30000, frames) < rms(output, 1000, 10000), "Reverb impulse produces a delayed, decaying diffuse tail.");
    require(rms(output, frames, output.size()) == 0, "Reverb channels do not share mutable delay state.");
    reverb.reset();
    require(render(reverb, impulse, 2, 137) == output, "Reverb state/reset is deterministic across partitions.");
    prepare(reverb, 96000);
    require(rms(render(reverb, std::vector<float>(4096)), 0, 4096) == 0, "Rate change clears old reverb state.");
}

/** Test source demonstrating GeneratorNode without introducing an unrequested product oscillator. */
class ConstantGenerator final : public GeneratorNode
{
    float sample(int, float) noexcept override { return 0.25f; }
};
/** One-sample wet path demonstrates that bypass dry compensation preserves declared latency. */
class LatentEffect final : public EffectNode
{
public:
    int latencyFrames() const noexcept override { return 1; }
private:
    void resetState() noexcept override { previous_ = 0; }
    float sample(int, float input) noexcept override { const auto output = previous_; previous_ = input; return output; }
    float previous_{};
};
void testBypassAndBases()
{
    GainNode gain(0);
    prepare(gain);
    event(gain, AudioNode::bypassParameter, 1, ParameterAction::base);
    const auto bypass = render(gain, std::vector<float>(512, 1), 1, 47);
    require(bypass[0] > 0 && bypass[0] < 0.005f && bypass[239] == 1, "Bypass crossfades smoothly to dry over five milliseconds.");
    for (std::size_t i = 1; i < bypass.size(); ++i)
        require(std::abs(bypass[i] - bypass[i - 1]) < 0.005f, "Bypass has no coefficient discontinuity.");
    event(gain, AudioNode::bypassParameter, 0, ParameterAction::base);
    require(render(gain, std::vector<float>(512, 1)).back() == 0, "Unbypass returns to wet while preserving layout.");
    event(gain, AudioNode::bypassParameter, 1);
    require(render(gain, std::vector<float>(512, 1)).back() == 1, "Discrete bypass supports scheduled automation targets.");
    event(gain, AudioNode::bypassParameter, 0, ParameterAction::overrideValue);
    require(render(gain, std::vector<float>(512, 1)).back() == 0, "Bypass has explicit live-override precedence.");
    event(gain, AudioNode::bypassParameter, 0, ParameterAction::releaseOverride);
    require(render(gain, std::vector<float>(512, 1)).back() == 1, "Bypass release resumes automated state without interpolating the boolean.");
    LatentEffect latent;
    prepare(latent);
    event(latent, AudioNode::bypassParameter, 1, ParameterAction::base);
    std::vector<float> sequence(512);
    for (std::size_t i = 0; i < sequence.size(); ++i) sequence[i] = static_cast<float>(i) / 512;
    const auto aligned = render(latent, sequence);
    require(aligned[0] == 0, "Latency compensated bypass starts from initialized silence.");
    for (std::size_t i = 1; i < aligned.size(); ++i)
        require(near(aligned[i], sequence[i - 1]), "Dry/wet bypass paths remain sample-aligned.");
    ConstantGenerator generator;
    prepare(generator);
    const auto generated = render(generator, std::vector<float>(512, 99));
    require(generated[0] == 0.25f, "Generator ignores an accidental upstream audio input.");
    event(generator, AudioNode::bypassParameter, 1, ParameterAction::base);
    require(render(generator, std::vector<float>(512, 99)).back() == 0, "Generator bypass fades to silence, not an input route.");
    AnalyzerNode analyzer;
    prepare(analyzer, 48000, 2);
    std::vector<float> signal(1024, 0.5f);
    std::fill(signal.begin() + 512, signal.end(), -0.25f);
    require(render(analyzer, signal, 2, 17) == signal, "Analyzer base passes through audio.");
    const auto snapshot = analyzer.snapshot();
    require(snapshot.windows == 0, "Unobserved analyzer does no statistics on the callback; worker calibration is covered by visualization tests.");
    analyzer.reset();
    require(analyzer.snapshot().windows == 0, "Analyzer reset clears observation epoch.");
    std::atomic<bool> corrupt{};
    std::thread renderThread([&]
    {
        std::array<float, 256> input{}, output{};
        input.fill(0.5f);
        const AudioBuffer hardware;
        // Fixed work count, no lock or allocation in the renderer or snapshot handoff.
        for (int window = 0; window < 1000; ++window)
        {
            std::uint64_t allocations{}, frees{};
            {
                allocationProbe::Scope probe;
                analyzer.process({input.data(), output.data(), 2, 128, 128, hardware});
                allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
            }
            if (allocations || frees) corrupt.store(true);
        }
    });
    for (int read = 0; read < 10000; ++read)
    {
        const auto observed = analyzer.snapshot();
        if (!std::isfinite(observed.rms[0]) || observed.peak[0] < 0 || observed.peak[0] > 0.5f)
            corrupt.store(true);
    }
    renderThread.join();
    require(!corrupt.load() && analyzer.snapshot().windows == 0, "Unarmed analyzer has no observation work or callback allocations.");
}

void testProtection()
{
    std::vector<std::unique_ptr<AudioNode>> nodes;
    nodes.push_back(std::make_unique<GainNode>());
    nodes.push_back(std::make_unique<EqNode>());
    nodes.push_back(std::make_unique<DelayNode>());
    nodes.push_back(std::make_unique<CompressorNode>());
    nodes.push_back(std::make_unique<DistortionNode>());
    nodes.push_back(std::make_unique<ChorusNode>());
    nodes.push_back(std::make_unique<ReverbNode>());
    for (auto& node : nodes)
    {
        prepare(*node);
        std::vector<float> bad(2048, 0);
        bad[0] = std::numeric_limits<float>::quiet_NaN();
        bad[1] = std::numeric_limits<float>::infinity();
        bad[2] = 1.0e-39f;
        const auto output = render(*node, bad);
        require(std::all_of(output.begin(), output.end(), [](float sample) { return std::isfinite(sample); })
                && node->numericalFaults() == 2, "All processors contain NaN/Inf and report numerical faults without poisoning histories.");
        for (auto value : output) require(value == 0 || std::abs(value) >= 1.0e-20f, "Subnormal tails are flushed portably.");
        require(!node->prepare({0, 512, 1}).succeeded && !node->prepare({48000, 4097, 1}).succeeded,
                "Invalid prepare is an explicit error.");
    }
}

AudioGraph graph(AudioNodeType effect = AudioNodeType::gain, float gain = 0)
{ return {1, {{1, AudioNodeType::input}, {2, effect, gain}, {3, AudioNodeType::output}}, {{1, 2}, {2, 3}}}; }

void engineProcess(AudioEngine& engine, float* input, float* output, int frames)
{
    const float* inputs[]{input};
    float* outputs[]{output};
    std::uint64_t allocations{}, frees{};
    {
        allocationProbe::Scope probe;
        engine.process({inputs, outputs, 1, 1, frames});
        allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
    }
    require(allocations == 0 && frees == 0, "Engine automation/split processing allocates or frees no C++ memory.");
}
std::vector<float> automated(int block)
{
    AudioEngine engine;
    require(engine.startOffline({48000, 128, 512, 1, 1}, graph(), 1).succeeded, "Prepare automated graph.");
    const auto epoch = engine.telemetry().value->epoch;
    require(!engine.scheduleParameter(epoch, 1, 2, 0, 480001, 1).succeeded, "Keep distant schedules outside the bounded ten-second render horizon.");
    require(engine.scheduleParameter(epoch, 1, 2, 0, 10, 0.5f).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 10, 1).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 300, 0.25f, ParameterAction::base).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 300, 0.5f, ParameterAction::overrideValue).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 600, 0, ParameterAction::releaseOverride).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 900, 0, ParameterAction::disableAutomation).succeeded,
            "Schedule targets/overrides/release in absolute sample order.");
    std::vector<float> input(1200, 1), output(1200, 99);
    for (int offset = 0; offset < 1200; offset += block)
        engineProcess(engine, input.data() + offset, output.data() + offset, std::min(block, 1200 - offset));
    const auto report = *engine.telemetry().value;
    require(output[9] == 0 && near(output[10], 1.0 / 240) && output[249] == 1 && output[599] == 0.5f
            && output[899] == 1 && output[1199] == 0.25f, "Targets begin at exact in-block sample offsets; same-frame last acceptance wins.");
    require(report.samplePosition == 1200 && report.appliedEvents == 6 && report.missedEvents == 0
            && report.lastEventSequence == 6 && report.lastEventFrame == 900, "Applied sequence/position telemetry is explicit.");
    require(!engine.scheduleParameter(epoch, 1, 2, 0, 1100, 1).succeeded
            && !engine.scheduleParameter(epoch + 1, 1, 2, 0, 1300, 1).succeeded,
            "Late schedules and wrong session epochs are rejected, not replayed.");
    require(engine.stop().succeeded && engine.startOffline({48000, 128, 512, 1, 1}, graph(), 1).succeeded
            && engine.telemetry().value->epoch != epoch
            && !engine.scheduleParameter(epoch, 1, 2, 0, 10, 1).succeeded, "Restart resets sample clock and rejects events from earlier sessions.");
    return output;
}
void testAutomationAndGraph()
{
    require(automated(128) == automated(37), "Sample-accurate automation is invariant to host block size.");
    AudioEngine engine;
    require(engine.startOffline({48000, 128, 512, 1, 1}, graph(), 1).succeeded, "Start bounded scheduler.");
    const auto epoch = engine.telemetry().value->epoch;
    for (int i = 0; i < 128; ++i)
        require(engine.scheduleParameter(epoch, 1, 2, 0, 0, static_cast<float>(i % 2)).succeeded, "Declared 128-event capacity.");
    require(!engine.scheduleParameter(epoch, 1, 2, 0, 0, 1).succeeded, "Schedule overflow returns error before losing any event.");
    for (int i = 0; i < 32; ++i)
        require(engine.setParameter(1, 2, 0, static_cast<float>(i % 2)).succeeded, "Declared independent 32-live-event capacity.");
    require(!engine.setParameter(1, 2, 0, 1).succeeded, "Live overflow remains an explicit error even while scheduled storage is full.");
    std::array<float, 512> input{}, output{};
    input.fill(1);
    engineProcess(engine, input.data(), output.data(), 0);
    require(engine.telemetry().value->appliedEvents == 0, "Zero-frame calls do not consume scheduled events.");
    engineProcess(engine, input.data(), output.data(), 128);
    require(engine.telemetry().value->appliedEvents == 160 && engine.telemetry().value->lastEventSequence == 160,
            "All 160 same-frame events are ordered and applied without coalescing away transitions.");
    require(engine.scheduleParameter(epoch, 1, 2, 0, 4800, 1).succeeded
            && !engine.scheduleParameter(epoch, 1, 2, 0, 4799, 1).succeeded, "Out-of-order scheduled frames are rejected.");
    require(engine.publish(graph(), 2).succeeded, "Publish replacement with stable reused identities.");
    require(engine.scheduleParameter(epoch, 2, 2, 0, 128, 0.5f).succeeded,
            "A new revision can schedule now rather than waiting for its cancelled predecessor's distant timestamps.");
    engineProcess(engine, input.data(), output.data(), 128);
    require(engine.telemetry().value->canceledEvents == 1, "Old-revision schedule cannot mutate a reused node.");
    for (const auto type : {AudioNodeType::eq, AudioNodeType::delay, AudioNodeType::compressor,
                            AudioNodeType::distortion, AudioNodeType::chorus, AudioNodeType::reverb, AudioNodeType::analyzer})
    {
        const auto revision = static_cast<std::uint64_t>(type) + 10;
        require(engine.publish(graph(type, 1), revision).succeeded, "All built-ins prepare through the existing graph pipeline.");
        engineProcess(engine, input.data(), output.data(), 512);
        for (auto value : output) require(std::isfinite(value), "Integrated processor graph produces finite samples.");
    }
    const auto revision = static_cast<std::uint64_t>(AudioNodeType::analyzer) + 10;
    require(engine.analyzerSnapshot(revision, 2).value->windows == 0
            && !engine.analyzerSnapshot(revision, 1).value, "Analyzer identity is retained without implicit audio-thread analysis.");
    input[0] = std::numeric_limits<float>::quiet_NaN();
    engineProcess(engine, input.data(), output.data(), 128);
    require(output[0] == 0 && engine.telemetry().value->latestPlanNumericalFaults == 1,
            "Sanitized hardware-input faults are visible in engine telemetry, not hidden.");
    val::domain::Graph structural;
    structural.nodes.emplace(1, val::domain::Node{1, "Not an inferred processor name"});
    structural.nodes.at(1).parameters["audio.type"] = std::string("Delay");
    structural.nodes.at(1).parameters["audio.param.seconds"] = 0.5;
    structural.nodes.at(1).parameters["audio.bypass"] = true;
    const auto mapped = AudioGraph::fromStructural(structural, 1);
    require(mapped.value && mapped.value->nodes[0].parameters[0].second == 0.5f && mapped.value->nodes[0].bypassed,
            "Persisted typed effect parameters and bypass map explicitly.");
    structural.nodes.at(1).parameters["audio.param.seconds"] = 3.0;
    require(!AudioGraph::fromStructural(structural, 1).value, "Out-of-range persisted effect data is rejected.");
    auto invalid = graph(); invalid.nodes[1].parameters = {{std::uint16_t{0}, 0.5f}, {std::uint16_t{0}, 0.25f}};
    require(!PreparedAudioGraph::prepare(invalid, {48000, 128, 512, 1, 1}, 1).value, "Duplicate initial parameters cannot hide conflicting values.");
    auto expensive = graph(AudioNodeType::delay);
    expensive.channels = 32;
    expensive.nodes.insert(expensive.nodes.begin() + 2, {4, AudioNodeType::delay});
    expensive.connections = {{1, 2}, {2, 4}, {4, 3}};
    require(!PreparedAudioGraph::prepare(expensive, {384000, 128, 512, 32, 32}, 1).value,
            "DSP delay storage is bounded by the aggregate prepared-plan budget.");
}
void testLiveScheduledOrdering()
{
    AudioEngine engine;
    require(engine.startOffline({48000, 128, 512, 1, 1}, graph(), 1).succeeded, "Prepare mixed parameter handoff.");
    const auto epoch = engine.telemetry().value->epoch;
    require(engine.scheduleParameter(epoch, 1, 2, 0, 0, 0.75f, ParameterAction::overrideValue).succeeded
            && engine.setParameter(1, 2, 0, 0, ParameterAction::releaseOverride).succeeded,
            "Accept scheduled override followed by unscheduled release.");
    std::array<float, 512> input{}, output{};
    input.fill(1);
    engineProcess(engine, input.data(), output.data(), 512);
    require(output.back() == 0 && engine.telemetry().value->appliedEvents == 2,
            "Live and scheduled same-frame transitions share acceptance order, not queue priority.");
    require(engine.setParameter(1, 2, 0, 0.75f).succeeded
            && engine.scheduleParameter(epoch, 1, 2, 0, 512, 0, ParameterAction::releaseOverride).succeeded,
            "Accept live override before later accepted same-frame release.");
    engineProcess(engine, input.data(), output.data(), 512);
    require(output.back() == 0, "Reverse queue admission also preserves total same-frame order.");
    require(engine.stop().succeeded && engine.startOffline({48000, 128, 512, 1, 1}, graph(), 1).succeeded,
            "Start a new block-end timing epoch.");
    const auto nextEpoch = engine.telemetry().value->epoch;
    require(engine.scheduleParameter(nextEpoch, 1, 2, 0, 128, 1).succeeded, "Schedule at the first block's exclusive end.");
    engineProcess(engine, input.data(), output.data(), 128);
    require(engine.telemetry().value->appliedEvents == 0 && output[127] == 0, "Exclusive end event does not affect the preceding block.");
    engineProcess(engine, input.data(), output.data(), 1);
    require(near(output[0], 1.0 / 240) && engine.telemetry().value->lastEventFrame == 128,
            "An exclusive-end event begins at sample zero of the next block.");
}

void testAllProcessorWorkload()
{
    AudioGraph pipeline;
    pipeline.channels = 2;
    pipeline.nodes = {{1, AudioNodeType::input}, {2, AudioNodeType::gain, 0.5f},
        {3, AudioNodeType::eq}, {4, AudioNodeType::delay}, {5, AudioNodeType::compressor},
        {6, AudioNodeType::distortion}, {7, AudioNodeType::chorus}, {8, AudioNodeType::reverb},
        {9, AudioNodeType::analyzer}, {10, AudioNodeType::output}};
    for (std::uint64_t i = 1; i < 10; ++i) pipeline.connections.push_back({i, i + 1});
    AudioEngine engine;
    require(engine.startOffline({48000, 128, 512, 2, 2}, pipeline, 1).succeeded, "Prepare a real seven-processor stereo pipeline.");
    const auto epoch = engine.telemetry().value->epoch;
    for (std::uint64_t event = 0; event < 128; ++event)
        require(engine.scheduleParameter(epoch, 1, 3, 1, 100 + event * 127,
                                        event % 2 ? 6.0f : -6.0f).succeeded, "Schedule maximum bounded EQ automation horizon.");
    std::array<float, 128> left{}, right{}, outLeft{}, outRight{};
    const float* input[]{left.data(), right.data()};
    float* output[]{outLeft.data(), outRight.data()};
    const auto began = std::chrono::steady_clock::now();
    for (int block = 0; block < 2000; ++block)
    {
        for (int i = 0; i < 128; ++i)
        {
            left[i] = static_cast<float>(0.1 * std::sin(2 * std::numbers::pi * 997 * (block * 128 + i) / 48000));
            right[i] = left[i] * 0.5f;
        }
        std::uint64_t allocations{}, frees{};
        {
            allocationProbe::Scope probe;
            engine.process({input, output, 2, 2, 128});
            allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
        }
        require(allocations == 0 && frees == 0, "Full effect chain plus automation allocates/frees no callback C++ memory.");
        for (auto sample : outLeft) require(std::isfinite(sample), "Effect pipeline stays finite.");
    }
    const auto report = *engine.telemetry().value;
    require(report.appliedEvents == 128 && report.latestPlanNumericalFaults == 0 && report.samplePosition == 256000
            && engine.analyzerSnapshot(1, 9).value->windows == 0, "Long-run integrated state and automation remain bounded without unarmed observation.");
    std::cout << "Offline seven-effect stereo workload: 2000 x 128 frames, 128 scheduled events; "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count()
              << " s; last/max engine callback ns " << report.callbackNanoseconds << "/" << report.maxCallbackNanoseconds
              << "; average deadline load " << report.averageCallbackLoad << "; duration-based misses "
              << report.deadlineMisses << "; zero instrumented callback C++ allocations/frees; NOT hardware performance.\n";
}
}

int main()
{
    return runTests([]
    {
        testGainAndLayers(); testEq(); testDelay(); testCompressor(); testDistortion();
        testChorus(); testReverb(); testBypassAndBases(); testProtection(); testAutomationAndGraph();
        testLiveScheduledOrdering(); testAllProcessorWorkload();
        std::cout << "Seven real processors, unified bases, automation, bypass, rates, state and callback allocations verified offline.\n";
    });
}
