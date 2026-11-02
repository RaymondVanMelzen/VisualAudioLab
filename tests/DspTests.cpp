#include "TestSupport.h"
#include "AudioAllocationProbe.h"
#include <val/audio/AudioEngine.h>
#include <val/audio/AudioGraph.h>
#include <val/audio/GeneratorNodes.h>
#include <val/script/Compiler.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
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
{ require(node.initialParameter(parameter, value), "Validate initial physical-domain parameter "
          + std::to_string(parameter) + "=" + std::to_string(value) + "."); }
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
double binPower(const std::vector<float>& values, std::size_t start, int count, int bin)
{
    const auto angle = 2.0 * std::numbers::pi * bin / static_cast<double>(count);
    const auto stepCos = std::cos(angle), stepSin = std::sin(angle);
    double cosine = 1, sine{}, real{}, imaginary{};
    for (int i = 0; i < count; ++i)
    {
        const auto sample = values[start + static_cast<std::size_t>(i)];
        real += sample * cosine;
        imaginary -= sample * sine;
        const auto nextCosine = cosine * stepCos - sine * stepSin;
        sine = sine * stepCos + cosine * stepSin;
        cosine = nextCosine;
    }
    return (real * real + imaginary * imaginary) / (static_cast<double>(count) * count);
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

std::vector<float> renderSource(AudioNode& node, int frames, int block)
{
    std::vector<float> output(static_cast<std::size_t>(frames), 99.0f);
    const AudioBuffer hardware{};
    for (int offset = 0; offset < frames; offset += block)
        process(node, {nullptr, output.data() + offset, 1, frames, std::min(block, frames - offset), hardware, offset});
    return output;
}

void testGeneratorSources()
{
    const auto hzEntry = parseFrequencyEntry("440 Hz", 440);
    const auto midiEntry = parseFrequencyEntry("MIDI 69", 440);
    const auto noteEntry = parseFrequencyEntry("a4", 440);
    const auto tunedEntry = parseFrequencyEntry("A4 (A=442 Hz)", 440);
    require(hzEntry && midiEntry && noteEntry && tunedEntry
            && std::abs(hzEntry.hertz - 440) < 1.0e-9
            && std::abs(midiEntry.hertz - 440) < 1.0e-9
            && std::abs(noteEntry.hertz - 440) < 1.0e-9
            && std::abs(tunedEntry.hertz - 442) < 1.0e-9,
            "Frequency entry converts Hz, MIDI and explicit note-reference tuning unambiguously.");
    require(!parseFrequencyEntry("A442", 440) && !parseFrequencyEntry("MIDI 69.5", 440)
            && !parseFrequencyEntry("C11", 440),
            "Ambiguous note spellings, fractional MIDI notes and out-of-range note octaves are rejected.");

    SignalGeneratorNode oscillator;
    initial(oscillator, signalFrequency, 1000);
    initial(oscillator, signalLevelDb, -6.0205999f);
    prepare(oscillator, 48000, 1);
    const auto tone = renderSource(oscillator, 480, 17);
    require(std::abs(tone[12] - 0.5f) < 1.0e-5f && std::abs(tone[24]) < 1.0e-5f,
            "Signal Generator produces the requested 1 kHz sine at a -6.0206 dBFS peak.");

    const auto configuredSweep = [](SignalGeneratorNode& node)
    {
        initial(node, signalMode, static_cast<float>(SignalMode::sweep));
        initial(node, signalStartFrequency, 100);
        initial(node, signalEndFrequency, 5000);
        initial(node, signalDuration, 0.25f);
        initial(node, signalSweepCurve, static_cast<float>(SweepCurve::exponential));
        initial(node, signalCurvature, 4);
        initial(node, signalTransport, static_cast<float>(GeneratorTransport::loop));
        prepare(node, 48000, 1);
    };
    SignalGeneratorNode sweepA, sweepB;
    configuredSweep(sweepA); configuredSweep(sweepB);
    const auto a = renderSource(sweepA, 24000, 64);
    const auto b = renderSource(sweepB, 24000, 127);
    require(a == b, "Analytic sweep phase and loop boundaries are invariant to arbitrary processing blocks.");
    for (const auto rate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const auto curve : {SweepCurve::linear, SweepCurve::logarithmic, SweepCurve::exponential})
        {
            SignalGeneratorNode left, right;
            const auto configure = [&](SignalGeneratorNode& node)
            {
                initial(node, signalMode, static_cast<float>(SignalMode::sweep));
                initial(node, signalStartFrequency, 200);
                initial(node, signalEndFrequency, 12000);
                initial(node, signalDuration, 0.125f);
                initial(node, signalSweepCurve, static_cast<float>(curve));
                initial(node, signalCurvature, 3.0f);
                initial(node, signalTransport, static_cast<float>(GeneratorTransport::loop));
                prepare(node, rate, 1);
            };
            configure(left); configure(right);
            const auto frames = static_cast<int>(rate * 0.25);
            require(renderSource(left, frames, 97) == renderSource(right, frames, 511),
                    "All sweep curves preserve phase and loop boundaries across rates and block partitions.");
        }

    SignalGeneratorNode oneShot;
    initial(oneShot, signalMode, static_cast<float>(SignalMode::sweep));
    initial(oneShot, signalStartFrequency, 440);
    initial(oneShot, signalEndFrequency, 880);
    initial(oneShot, signalDuration, 0.01f);
    initial(oneShot, signalTransport, static_cast<float>(GeneratorTransport::oneShot));
    prepare(oneShot, 48000, 1);
    const auto oneShotSamples = renderSource(oneShot, 700, 73);
    require(std::any_of(oneShotSamples.begin(), oneShotSamples.begin() + 480, [](float sample) { return sample != 0; })
            && std::all_of(oneShotSamples.begin() + 480, oneShotSamples.end(), [](float sample) { return sample == 0; }),
            "One-shot sweep renders exactly its nearest-sample duration, then remains silent.");

    SignalGeneratorNode customScale;
    constexpr int scaleOffsets[]{0, 2, 7};
    require(customScale.setUserScale(scaleOffsets) && !customScale.setUserScale(std::array{0, 2, 2}),
            "Custom musical scales require ascending unique semitone offsets.");
    initial(customScale, signalMode, static_cast<float>(SignalMode::musical));
    initial(customScale, signalTransport, static_cast<float>(GeneratorTransport::oneShot));
    initial(customScale, signalScale, static_cast<float>(MusicalScale::user));
    initial(customScale, signalRootMidi, 60);
    initial(customScale, signalStepSeconds, 0.01f);
    initial(customScale, signalDuration, 1);
    initial(customScale, signalLevelDb, 0);
    initial(customScale, signalReferenceAHz, 442);
    prepare(customScale, 48000, 1);
    const auto scaleSignal = renderSource(customScale, 1101, 73);
    const auto midiFrequency = [](int midi) { return 442.0 * std::exp2((midi - 69) / 12.0); };
    const auto expectedPhase = (480.0 * midiFrequency(60) + 480.0 * midiFrequency(62)
                                + 140.0 * midiFrequency(67)) / 48000.0;
    require(std::abs(scaleSignal[1100] - std::sin(2.0 * std::numbers::pi * expectedPhase)) < 1.0e-4,
            "User musical scale steps use the configured root, exact semitone set, A-reference and sample timing.");

    DcGeneratorNode dc;
    initial(dc, 0, 0.25f);
    prepare(dc);
    const auto constant = renderSource(dc, 257, 31);
    require(std::all_of(constant.begin(), constant.end(), [](float sample) { return sample == 0.25f; }),
            "DC output is an exact stable value, not inferred from a frequency or dB control.");
    require(dc.parameterEvent(1, 1, ParameterAction::base), "DC mute is a discrete parameter.");
    const auto mutedDc = renderSource(dc, 32, 7);
    require(std::all_of(mutedDc.begin(), mutedDc.end(), [](float sample) { return sample == 0.0f; }),
            "Muted DC emits exact digital silence.");

    ImpulseGeneratorNode impulse;
    initial(impulse, 0, 0);
    initial(impulse, 1, 0.002f);
    initial(impulse, 2, 1);
    prepare(impulse);
    const auto impulses = renderSource(impulse, 256, 19);
    require(impulses[0] == 1 && impulses[96] == 1 && impulses[192] == 1
            && impulses[1] == 0 && impulses[95] == 0,
            "Periodic impulse placement is sample-exact at the rounded 2 ms interval.");

    ClickGeneratorNode clickA, clickB;
    initial(clickA, 0, 300);
    initial(clickA, 2, 0.005f);
    initial(clickA, 3, 0);
    initial(clickB, 0, 300);
    initial(clickB, 2, 0.005f);
    initial(clickB, 3, 0);
    prepare(clickA); prepare(clickB);
    const auto clicksA = renderSource(clickA, 19202, 97);
    const auto clicksB = renderSource(clickB, 19202, 511);
    require(clicksA == clicksB && clicksA[0] == 0 && clicksA[1] > 0
            && clicksA[9600] == 0 && clicksA[9601] > 0,
            "BPM clicks retrigger at their exact sample interval independent of host block size.");

    BurstGeneratorNode burst;
    initial(burst, 0, 1000);
    initial(burst, 1, 0.01f);
    initial(burst, 2, 0.02f);
    initial(burst, 3, 0);
    initial(burst, 4, 0.001f);
    prepare(burst);
    const auto burstSamples = renderSource(burst, 600, 73);
    require(burstSamples[0] == 0 && std::abs(burstSamples[36]) > 0.5f
            && std::all_of(burstSamples.begin() + 480, burstSamples.end(), [](float sample) { return sample == 0; }),
            "Burst is finite, edge-shaped, and exactly silent after its 10 ms one-shot duration.");
    BurstGeneratorNode overlappingBurst;
    initial(overlappingBurst, 1, 0.02f);
    initial(overlappingBurst, 2, 0.01f);
    initial(overlappingBurst, 5, 1);
    require(!overlappingBurst.prepare({48000, 512, 1}).succeeded,
            "Periodic burst setup rejects a duration that would overlap its interval.");

    ChirpGeneratorNode chirpA, chirpB;
    const auto configureChirp = [](ChirpGeneratorNode& node)
    {
        initial(node, 0, 200);
        initial(node, 1, 8000);
        initial(node, 2, 0.1f);
        initial(node, 3, static_cast<float>(SweepCurve::logarithmic));
        initial(node, 5, -12);
        initial(node, 6, static_cast<float>(GeneratorTransport::loop));
        prepare(node);
    };
    configureChirp(chirpA); configureChirp(chirpB);
    require(renderSource(chirpA, 12000, 89) == renderSource(chirpB, 12000, 512),
            "Chirp phase remains continuous and partition-invariant through repeated log sweeps.");

    NoiseGeneratorNode noise;
    initial(noise, 0, 0);
    initial(noise, 1, -20);
    initial(noise, 2, 7331);
    prepare(noise);
    const auto white = renderSource(noise, 65536, 113);
    const auto whiteRms = rms(white, 0, white.size());
    noise.reset();
    require(white == renderSource(noise, 65536, 127), "A seed reset reproduces the exact per-instance white-noise sequence.");
    require(std::abs(whiteRms - 0.1) < 0.002,
            "White noise level is calibrated to its declared -20 dBFS RMS expectation.");
    require(noise.parameterEvent(0, 1, ParameterAction::base), "Pink noise selection is a discrete validated choice.");
    const auto pink = renderSource(noise, 65536, 257);
    const auto pinkRms = rms(pink, 0, pink.size());
    require(std::isfinite(pinkRms) && pinkRms > 0.05 && pinkRms < 0.15,
            "Pink-noise output is finite and remains near its requested RMS level (measured "
                + std::to_string(pinkRms) + ").");
    const int pinkWindow = 32768;
    double previousOctaveDensity{};
    std::array<double, 7> adjacentOctaveDb{};
    std::size_t octavePair{};
    for (int centreBin : {32, 64, 128, 256, 512, 1024, 2048, 4096})
    {
        double bandPower{};
        for (int bin = centreBin - 2; bin <= centreBin + 2; ++bin)
            bandPower += binPower(pink, 0, pinkWindow, bin);
        const auto octaveDensity = bandPower * centreBin / 5.0;
        if (previousOctaveDensity > 0)
        {
            const auto deviationDb = 10.0 * std::log10(octaveDensity / previousOctaveDensity);
            adjacentOctaveDb[octavePair++] = deviationDb;
            require(std::abs(deviationDb) < 6.0,
                    "Measured filtered-pink noise remains within 6 dB per octave ("
                        + std::to_string(deviationDb) + " dB).");
        }
        previousOctaveDensity = octaveDensity;
    }
    std::cout << std::setprecision(8) << std::scientific
              << "SYNTHETIC_NOISE rate=48000 seed=7331 frames=65536 white_rms=" << whiteRms
              << " pink_rms=" << pinkRms << " adjacent_octave_density_dB=";
    for (std::size_t index = 0; index < adjacentOctaveDb.size(); ++index)
        std::cout << (index == 0 ? "" : ",") << adjacentOctaveDb[index];
    std::cout << std::defaultfloat << '\n';

    auto sourceCapture = std::make_shared<SignalCaptureBuffer>(11, 48000, 1);
    sourceCapture->setEnabled(true);
    AudioGraph outputOnly{1, {{11, AudioNodeType::signalGenerator}, {12, AudioNodeType::output}}, {{11, 12}}};
    outputOnly.nodes[0].capture = sourceCapture;
    AudioEngine engine;
    require(engine.startOffline({48000, 64, 256, 0, 1}, outputOnly, 1).succeeded,
            "A zero-input signal generator can prepare and start an output-only graph.");
    std::array<float, 64> output{};
    float* outputs[]{output.data()};
    engine.process({nullptr, outputs, 0, 1, static_cast<int>(output.size())});
    require(std::any_of(output.begin(), output.end(), [](float sample) { return sample != 0; }),
            "Output-only generator graph produces signal without a device input.");
    const auto* firstCapture = sourceCapture->acquire();
    require(firstCapture && firstCapture->stamp.nodeFrame == 0,
            "Captured generator progress begins at the exact prepared-node frame zero.");
    sourceCapture->release();
    engine.process({nullptr, outputs, 0, 1, static_cast<int>(output.size())});
    const auto* nextCapture = sourceCapture->acquire();
    require(nextCapture && nextCapture->stamp.firstFrame == 64 && nextCapture->stamp.nodeFrame == 64,
            "Captured progress distinguishes node-relative sweep time from session-relative sample position.");
    sourceCapture->release();
    require(engine.stop().succeeded, "Stop output-only offline test explicitly.");

    AudioGraph invalidSource{1,
        {{1, AudioNodeType::input}, {2, AudioNodeType::signalGenerator}, {3, AudioNodeType::output}},
        {{1, 2}, {2, 3}}};
    require(!PreparedAudioGraph::prepare(invalidSource, {48000, 64, 256, 1, 1}, 2).value,
            "Generator modules reject upstream audio rather than silently ignoring a connection.");
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
    structural.nodes.clear();
    structural.nodes.emplace(7, val::domain::Node{7, "User-scale source"});
    structural.nodes.at(7).parameters["audio.type"] = std::string("Signal Generator");
    structural.nodes.at(7).parameters["audio.param.scale"] = 5.0;
    structural.nodes.at(7).parameters["audio.param.rootMidi"] = 60.0;
    structural.nodes.at(7).parameters["audio.scaleIntervals"] = std::string("0, 2, 7");
    const auto customScaleGraph = AudioGraph::fromStructural(structural, 1);
    require(customScaleGraph.value && customScaleGraph.value->nodes[0].userScale == std::vector<int>{0, 2, 7},
            "Saved custom scales map into a validated source-node definition.");
    structural.nodes.at(7).parameters["audio.scaleIntervals"] = std::string("0, 2, 2");
    require(!AudioGraph::fromStructural(structural, 1).value,
            "Invalid saved custom scale offsets are rejected instead of falling back to chromatic.");
    structural.nodes.at(7).parameters["audio.scaleIntervals"] = std::string("0, 8");
    structural.nodes.at(7).parameters["audio.param.rootMidi"] = 120.0;
    require(!AudioGraph::fromStructural(structural, 1).value,
            "A custom scale cannot transpose beyond MIDI note 127.");
    auto invalid = graph(); invalid.nodes[1].parameters = {{std::uint16_t{0}, 0.5f}, {std::uint16_t{0}, 0.25f}};
    require(!PreparedAudioGraph::prepare(invalid, {48000, 128, 512, 1, 1}, 1).value, "Duplicate initial parameters cannot hide conflicting values.");
    auto expensive = graph(AudioNodeType::delay);
    expensive.channels = 32;
    expensive.nodes.insert(expensive.nodes.begin() + 2, {4, AudioNodeType::delay});
    expensive.connections = {{1, 2}, {2, 4}, {4, 3}};
    require(!PreparedAudioGraph::prepare(expensive, {384000, 128, 512, 32, 32}, 1).value,
            "DSP delay storage is bounded by the aggregate prepared-plan budget.");
}

void testTypedParameterControls()
{
    const auto configureGainControl = [](GainNode& gain, ParameterControlBinding binding)
    {
        const auto source = static_cast<int>(binding.source);
        require(gain.initialControl(std::move(binding)),
                "Accept valid typed scalar control mapping source " + std::to_string(source) + ".");
        prepare(gain);
    };

    GainNode lfoA(0.5f), lfoB(0.5f);
    ParameterControlBinding lfo;
    lfo.parameter = 0;
    lfo.source = ParameterControlSource::lfo;
    lfo.depth = 0.25f;
    lfo.rateHz = 1;
    configureGainControl(lfoA, lfo);
    configureGainControl(lfoB, lfo);
    const std::vector<float> unity(48001, 1);
    const auto lfoFirst = render(lfoA, unity, 1, 127);
    const auto lfoSecond = render(lfoB, unity, 1, 509);
    require(lfoFirst == lfoSecond && near(lfoFirst[0], 0.5)
            && near(lfoFirst[12000], 0.75) && near(lfoFirst[36000], 0.25),
            "LFO control is sample-time deterministic and independent of host block partitioning.");

    GainNode envelope(0);
    ParameterControlBinding env;
    env.parameter = 0;
    env.source = ParameterControlSource::envelope;
    env.depth = 1;
    env.sourceMinimum = 0;
    env.sourceMaximum = 1;
    env.attackSeconds = 0.01f;
    env.holdSeconds = 0.01f;
    env.releaseSeconds = 0.01f;
    configureGainControl(envelope, env);
    const auto envelopeOutput = render(envelope, std::vector<float>(2400, 1), 1, 73);
    require(envelopeOutput[0] == 0 && near(envelopeOutput[240], 0.5)
            && near(envelopeOutput[960], 1) && near(envelopeOutput[1200], 0.5)
            && envelopeOutput[1920] == 0,
            "One-shot Envelope control follows exact prepared attack, hold and release sample times ("
                + std::to_string(envelopeOutput[0]) + ", " + std::to_string(envelopeOutput[240]) + ", "
                + std::to_string(envelopeOutput[960]) + ", " + std::to_string(envelopeOutput[1200]) + ", "
                + std::to_string(envelopeOutput[1920]) + ").");

    const auto script = val::script::Compiler::compile(
        "NODE ScalarControl AT AUDIO;\nINPUT Time: REAL;\nOUTPUT Value: REAL;\n"
        "BEGIN\n  Value := Time;\nEND.");
    require(script.value.has_value(), "Compile stateless Time-to-scalar VAL Script control off callback.");
    GainNode scripted(0);
    ParameterControlBinding scriptBinding;
    scriptBinding.parameter = 0;
    scriptBinding.source = ParameterControlSource::script;
    scriptBinding.depth = 1;
    scriptBinding.sourceMinimum = 0;
    scriptBinding.sourceMaximum = 1;
    scriptBinding.program = *script.value;
    configureGainControl(scripted, std::move(scriptBinding));
    const auto scriptedOutput = render(scripted, std::vector<float>(32, 1), 1, 7);
    require(scriptedOutput[0] == 0 && scriptedOutput[1] > 0
            && near(scriptedOutput[31], 31.0 / 48000.0, 1.0e-8),
            "Stateless Script scalar control executes against monotonic sample time without audio inputs.");
    const auto invalidScript = val::script::Compiler::compile(
        "NODE BadControl AT AUDIO;\nINPUT Time: REAL;\nOUTPUT Value: REAL;\n"
        "BEGIN\n  Value := 2.0;\nEND.");
    require(invalidScript.value.has_value(), "Compile the deliberately out-of-range Script-control test.");
    GainNode failedControl(0.5f);
    auto invalidBinding = ParameterControlBinding{};
    invalidBinding.parameter = 0;
    invalidBinding.source = ParameterControlSource::script;
    invalidBinding.depth = 1;
    invalidBinding.sourceMinimum = 0;
    invalidBinding.sourceMaximum = 1;
    invalidBinding.program = *invalidScript.value;
    require(failedControl.initialControl(std::move(invalidBinding)),
            "Accept Script control with an explicit declared output range.");
    prepare(failedControl);
    const auto failedOutput = render(failedControl, std::vector<float>(32, 1), 1, 7);
    require(std::all_of(failedOutput.begin(), failedOutput.end(), [](float sample) { return sample == 0; })
            && failedControl.numericalFaults() == 1,
            "Out-of-range Script control latches a visible numerical fault and silences the affected processor instead of falling back.");

    GainNode outOfRange(0.5f);
    lfo.depth = 1;
    require(!outOfRange.initialControl(lfo),
            "Control mapping whose declared source range exceeds the target parameter domain is rejected before prepare.");

    val::domain::Graph structural;
    val::domain::Node gainNode{1, "Gain"};
    gainNode.parameters["audio.type"] = std::string("Gain");
    gainNode.parameters["audio.param.gain"] = 0.5;
    gainNode.parameters["audio.control.gain.source"] = std::string("lfo");
    gainNode.parameters["audio.control.gain.offset"] = 0.0;
    gainNode.parameters["audio.control.gain.depth"] = 0.2;
    gainNode.parameters["audio.control.gain.rateHz"] = 2.0;
    val::domain::Node outputNode{2, "Output"};
    outputNode.parameters["audio.type"] = std::string("Output");
    structural.nodes.emplace(1, std::move(gainNode));
    structural.nodes.emplace(2, std::move(outputNode));
    structural.connections.emplace(3, val::domain::Connection{
        3, {1, val::domain::Port::output}, {2, val::domain::Port::input}});
    const auto mapped = AudioGraph::fromStructural(structural, 1);
    bool planReady{};
    if (mapped.value)
    {
        const auto mappedPlan = PreparedAudioGraph::prepare(*mapped.value, {48000, 64, 1, 1, 1}, 1);
        planReady = mappedPlan.value != nullptr;
    }
    require(mapped.value && mapped.value->nodes[0].controls.size() == 1
            && planReady,
            "Project metadata restores and prepares a typed parameter-control mapping.");
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
        testChorus(); testReverb(); testBypassAndBases(); testProtection(); testGeneratorSources(); testAutomationAndGraph();
        testTypedParameterControls(); testLiveScheduledOrdering(); testAllProcessorWorkload();
        std::cout << "Seven real processors, unified bases, automation, bypass, rates, state and callback allocations verified offline.\n";
    });
}
