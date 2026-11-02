#include <val/audio/GeneratorNodes.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace val::audio
{
namespace
{
constexpr ParameterSpec signalParameters[]{
    {"waveform", 0, 6, 0, 0}, {"frequencyHz", 20, 20000, 440, 0.005},
    {"mode", 0, 2, 0, 0}, {"startFrequencyHz", 20, 20000, 20, 0.005},
    {"endFrequencyHz", 20, 20000, 20000, 0.005}, {"durationSeconds", 0.001f, 3600, 5, 0},
    {"sweepCurve", 0, 2, 1, 0}, {"curvature", -10, 10, 3, 0.005},
    {"stepSeconds", 0.01f, 10, 0.5f, 0}, {"scale", 0, 5, 0, 0},
    {"rootMidi", 0, 127, 36, 0}, {"levelDb", -80, 12, -60, 0.005},
    {"muted", 0, 1, 0, 0}, {"phaseInvert", 0, 1, 0, 0}, {"transport", 0, 2, 0, 0},
    {"dcValue", -1, 1, 0, 0}, {"referenceAHz", 400, 480, 440, 0}};
constexpr ParameterSpec impulseParameters[]{
    {"levelDb", -80, 12, -60, 0}, {"intervalSeconds", 0.001f, 60, 1, 0},
    {"mode", 0, 1, 0, 0}, {"muted", 0, 1, 0, 0}};
constexpr ParameterSpec clickParameters[]{
    {"bpm", 20, 300, 120, 0}, {"frequencyHz", 200, 8000, 1000, 0.005},
    {"durationSeconds", 0.0002f, 0.01f, 0.005f, 0}, {"levelDb", -80, 12, -60, 0.005},
    {"muted", 0, 1, 0, 0}, {"intervalMode", 0, 1, 0, 0}, {"intervalSeconds", 0.01f, 60, 0.5f, 0}};
constexpr ParameterSpec burstParameters[]{
    {"frequencyHz", 20, 20000, 1000, 0.005}, {"durationSeconds", 0.001f, 10, 0.1f, 0},
    {"intervalSeconds", 0.01f, 60, 1, 0}, {"levelDb", -80, 12, -60, 0.005},
    {"fadeSeconds", 0, 5, 0.005f, 0}, {"mode", 0, 1, 0, 0}, {"muted", 0, 1, 0, 0}};
constexpr ParameterSpec chirpParameters[]{
    {"startFrequencyHz", 20, 20000, 20, 0.005}, {"endFrequencyHz", 20, 20000, 20000, 0.005},
    {"durationSeconds", 0.001f, 3600, 5, 0}, {"sweepCurve", 0, 2, 1, 0},
    {"curvature", -10, 10, 3, 0.005}, {"levelDb", -80, 12, -60, 0.005},
    {"transport", 0, 2, 1, 0}, {"muted", 0, 1, 0, 0}};
constexpr ParameterSpec noiseParameters[]{
    {"color", 0, 1, 0, 0}, {"levelDbRms", -80, 12, -60, 0.005},
    {"seed", 0, 16777215, 1, 0}, {"muted", 0, 1, 0, 0}};
constexpr ParameterSpec dcParameters[]{
    {"value", -1, 1, 0, 0}, {"muted", 0, 1, 0, 0}};

bool integerIn(float value, int minimum, int maximum) noexcept
{
    return value >= static_cast<float>(minimum) && value <= static_cast<float>(maximum)
        && std::floor(value) == value;
}

std::uint64_t intervalFrames(double seconds, double rate) noexcept
{
    return static_cast<std::uint64_t>(std::max(1.0, std::floor(seconds * rate + 0.5)));
}

std::uint64_t randomNext(std::uint64_t& state) noexcept
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

float whiteSample(std::uint64_t& state) noexcept
{
    // The high 24 bits map to a deterministic uniform [-sqrt(3), sqrt(3)) process.
    constexpr float scale = 1.0f / 8388608.0f;
    return (static_cast<float>(randomNext(state) >> 40) * scale - 1.0f) * 1.7320508075688772f;
}

float pinkSample(std::uint64_t& random, std::array<float, 7>& state) noexcept
{
    const auto white = whiteSample(random);
    state[0] = 0.99886f * state[0] + white * 0.0555179f;
    state[1] = 0.99332f * state[1] + white * 0.0750759f;
    state[2] = 0.96900f * state[2] + white * 0.1538520f;
    state[3] = 0.86650f * state[3] + white * 0.3104856f;
    state[4] = 0.55000f * state[4] + white * 0.5329522f;
    state[5] = -0.7616f * state[5] - white * 0.0168980f;
    const auto pink = state[0] + state[1] + state[2] + state[3] + state[4] + state[5]
        + state[6] + white * 0.5362f;
    state[6] = white * 0.115926f;
    return pink * 0.3261f;
}

double curveFrequency(double start, double end, double position, int curve, double curvature) noexcept
{
    const auto u = std::clamp(position, 0.0, 1.0);
    if (curve == static_cast<int>(SweepCurve::logarithmic))
        return start * std::exp(u * std::log(end / start));
    if (curve == static_cast<int>(SweepCurve::exponential) && std::abs(curvature) > 1.0e-7)
        return start + (end - start) * std::expm1(curvature * u) / std::expm1(curvature);
    return start + (end - start) * u;
}

double integratedCurve(double start, double end, double position, int curve, double curvature) noexcept
{
    const auto u = std::clamp(position, 0.0, 1.0);
    if (curve == static_cast<int>(SweepCurve::logarithmic))
    {
        const auto logRatio = std::log(end / start);
        if (std::abs(logRatio) < 1.0e-8) return start * u;
        return start * std::expm1(logRatio * u) / logRatio;
    }
    if (curve == static_cast<int>(SweepCurve::exponential) && std::abs(curvature) > 1.0e-7)
    {
        const auto correction = (std::expm1(curvature * u) / curvature - u) / std::expm1(curvature);
        return start * u + (end - start) * correction;
    }
    return start * u + (end - start) * u * u * 0.5;
}

float waveSample(double phase, int waveform) noexcept
{
    const auto cycles = phase - std::floor(phase);
    switch (static_cast<SignalWaveform>(waveform))
    {
        case SignalWaveform::sine: return static_cast<float>(std::sin(2.0 * std::numbers::pi * cycles));
        case SignalWaveform::square: return cycles < 0.5 ? 1.0f : -1.0f;
        case SignalWaveform::triangle: return static_cast<float>(1.0 - 4.0 * std::abs(cycles - 0.5));
        case SignalWaveform::sawtooth: return static_cast<float>(2.0 * cycles - 1.0);
        case SignalWaveform::whiteNoise:
        case SignalWaveform::pinkNoise:
        case SignalWaveform::dc: break;
    }
    return 0.0f;
}

float amplitude(float levelDb) noexcept
{
    return static_cast<float>(std::pow(10.0, static_cast<double>(levelDb) / 20.0));
}

std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}

char lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool startsWithIgnoreCase(std::string_view value, std::string_view prefix) noexcept
{
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower(value[i]) != lower(prefix[i])) return false;
    return true;
}

bool parsePositive(std::string_view value, double& output) noexcept
{
    value = trim(value);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
        && std::isfinite(output) && output > 0;
}
}

FrequencyEntryResult parseFrequencyEntry(std::string_view input, double referenceAHz) noexcept
{
    auto text = trim(input);
    if (!std::isfinite(referenceAHz) || referenceAHz < 400 || referenceAHz > 480)
        return {0, "Reference A must be between 400 and 480 Hz."};
    if (text.empty()) return {0, "Enter a frequency in Hz, MIDI note, or named note."};

    auto frequencyText = text;
    if (frequencyText.size() >= 2
        && lower(frequencyText[frequencyText.size() - 2]) == 'h'
        && lower(frequencyText.back()) == 'z')
    {
        frequencyText.remove_suffix(2);
        frequencyText = trim(frequencyText);
    }
    double enteredHertz{};
    if (parsePositive(frequencyText, enteredHertz))
    {
        if (enteredHertz < 20 || enteredHertz > 20000)
            return {0, "Frequency must be within 20-20000 Hz."};
        return {enteredHertz, {}};
    }

    double tuning = referenceAHz;
    auto noteEnd = text.find('(');
    auto note = trim(text.substr(0, noteEnd));
    if (noteEnd != std::string_view::npos)
    {
        auto tuningText = trim(text.substr(noteEnd + 1));
        if (tuningText.empty() || tuningText.back() != ')')
            return {0, "Use an explicit tuning suffix such as A4 (A=442 Hz)."};
        tuningText.remove_suffix(1);
        tuningText = trim(tuningText);
        if (!startsWithIgnoreCase(tuningText, "a="))
            return {0, "The note tuning suffix must be written as A=<Hz>."};
        auto tuningValueText = trim(tuningText.substr(2));
        if (tuningValueText.size() >= 2 && lower(tuningValueText[tuningValueText.size() - 2]) == 'h'
            && lower(tuningValueText.back()) == 'z')
        {
            tuningValueText.remove_suffix(2);
            tuningValueText = trim(tuningValueText);
        }
        if (!parsePositive(tuningValueText, tuning) || tuning < 400 || tuning > 480)
            return {0, "Reference A must be between 400 and 480 Hz."};
    }

    int midi{};
    if (startsWithIgnoreCase(note, "midi "))
    {
        const auto value = trim(note.substr(5));
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), midi);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
            || midi < 0 || midi > 127)
            return {0, "MIDI note must be an integer from 0 to 127."};
    }
    else
    {
        if (note.size() < 2) return {0, "Use a note such as A4 or MIDI 69."};
        const auto letter = lower(note[0]);
        constexpr int semitones[]{9, 11, 0, 2, 4, 5, 7};
        if (letter < 'a' || letter > 'g') return {0, "Note names must start with A through G."};
        auto cursor = std::size_t{1};
        auto pitch = semitones[letter - 'a'];
        if (cursor < note.size() && (note[cursor] == '#' || note[cursor] == 'b' || note[cursor] == 'B'))
        {
            pitch += note[cursor] == '#' ? 1 : -1;
            ++cursor;
        }
        int octave{};
        const auto octaveText = note.substr(cursor);
        const auto parsed = std::from_chars(octaveText.data(), octaveText.data() + octaveText.size(), octave);
        if (octaveText.empty() || parsed.ec != std::errc{} || parsed.ptr != octaveText.data() + octaveText.size())
            return {0, "Named notes need an octave, for example A4 or C#3."};
        midi = (octave + 1) * 12 + pitch;
        if (midi < 0 || midi > 127)
            return {0, "Named note is outside the supported MIDI range 0-127."};
    }

    const auto hertz = tuning * std::exp2((midi - 69) / 12.0);
    if (!std::isfinite(hertz) || hertz < 20 || hertz > 20000)
        return {0, "Converted frequency must be within 20-20000 Hz."};
    return {hertz, {}};
}

UserScaleResult parseUserScale(std::string_view text) noexcept
{
    UserScaleResult result;
    while (!text.empty())
    {
        const auto separator = text.find(',');
        const auto token = trim(text.substr(0, separator));
        int value{};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()
            || value < 0 || value > 127 || (result.count > 0 && value <= result.offsets[result.count - 1])
            || result.count >= result.offsets.size())
            return {{}, 0, "Enter 1-12 ascending, unique semitone offsets from 0 to 127."};
        result.offsets[result.count++] = value;
        if (separator == std::string_view::npos) break;
        if (separator + 1 == text.size())
            return {{}, 0, "The custom scale cannot end with a comma."};
        text.remove_prefix(separator + 1);
    }
    if (result.count == 0) return {{}, 0, "Enter at least one semitone offset."};
    return result;
}

SignalGeneratorNode::SignalGeneratorNode() : GeneratorNode(signalParameters) {}
bool SignalGeneratorNode::setUserScale(std::span<const int> offsets) noexcept
{
    if (offsets.empty() || offsets.size() > userScale_.size()) return false;
    int previous = -1;
    for (const auto offset : offsets)
    {
        if (offset < 0 || offset > 127 || offset <= previous) return false;
        previous = offset;
    }
    userScaleCount_ = static_cast<int>(offsets.size());
    std::copy(offsets.begin(), offsets.end(), userScale_.begin());
    return true;
}
void SignalGeneratorNode::prepareState() {}
void SignalGeneratorNode::resetState() noexcept
{
    frame_ = 0;
    phase_ = 0;
    frameValue_ = 0;
    std::fill(pinkState_.begin(), pinkState_.end(), std::array<float, 7>{});
    for (std::size_t channel = 0; channel < random_.size(); ++channel)
        random_[channel] = 0x9e3779b97f4a7c15ULL ^ (static_cast<std::uint64_t>(channel + 1) * 0xbf58476d1ce4e5b9ULL);
}
bool SignalGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{
    switch (parameter)
    {
        case signalWaveform: return integerIn(target, 0, 6);
        case signalMode: return integerIn(target, 0, 2);
        case signalSweepCurve: return integerIn(target, 0, 2);
        case signalScale: return integerIn(target, 0, 5)
            && (target != static_cast<float>(MusicalScale::user) || userScaleCount_ > 0);
        case signalRootMidi: return integerIn(target, 0, 127);
        case signalMuted:
        case signalPhaseInvert: return target == 0.0f || target == 1.0f;
        case signalTransport: return integerIn(target, 0, 2);
        default: return true;
    }
}
double SignalGeneratorNode::sweepPhase(double position) const noexcept
{
    const auto frames = intervalFrames(value(signalDuration), spec_.sampleRate);
    const auto seconds = static_cast<double>(frames) / spec_.sampleRate;
    return seconds * integratedCurve(value(signalStartFrequency), value(signalEndFrequency), position,
                                     static_cast<int>(value(signalSweepCurve)), value(signalCurvature));
}
float SignalGeneratorNode::nextNoise(int channel, bool pink) noexcept
{
    return pink ? pinkSample(random_[static_cast<std::size_t>(channel)], pinkState_[static_cast<std::size_t>(channel)])
                : whiteSample(random_[static_cast<std::size_t>(channel)]);
}
void SignalGeneratorNode::beginFrame(std::span<const float>) noexcept
{
    const auto waveform = static_cast<int>(value(signalWaveform));
    const auto mode = static_cast<SignalMode>(static_cast<int>(value(signalMode)));
    if (mode == SignalMode::fixed)
        frameValue_ = waveSample(phase_, waveform);
    else
    {
        const auto frames = intervalFrames(value(signalDuration), spec_.sampleRate);
        const auto transport = static_cast<GeneratorTransport>(static_cast<int>(value(signalTransport)));
        const auto loopCount = transport == GeneratorTransport::loop ? frame_ / frames : 0;
        const auto offset = transport == GeneratorTransport::loop ? frame_ % frames : std::min(frame_, frames);
        if (transport == GeneratorTransport::oneShot && frame_ >= frames)
        {
            frameValue_ = 0;
            return;
        }
        double position = static_cast<double>(offset) / static_cast<double>(frames);
        if (mode == SignalMode::musical)
        {
            constexpr int major[]{0, 2, 4, 5, 7, 9, 11};
            constexpr int minor[]{0, 2, 3, 5, 7, 8, 10};
            constexpr int majorPentatonic[]{0, 2, 4, 7, 9};
            constexpr int minorPentatonic[]{0, 3, 5, 7, 10};
            const auto scale = static_cast<MusicalScale>(static_cast<int>(value(signalScale)));
            const auto stepFrames = intervalFrames(value(signalStepSeconds), spec_.sampleRate);
            const auto musicalFrame = transport == GeneratorTransport::loop ? offset : frame_;
            const auto step = musicalFrame / stepFrames;
            const int* offsets = nullptr;
            int count = 12;
            if (scale == MusicalScale::major) { offsets = major; count = 7; }
            else if (scale == MusicalScale::naturalMinor) { offsets = minor; count = 7; }
            else if (scale == MusicalScale::majorPentatonic) { offsets = majorPentatonic; count = 5; }
            else if (scale == MusicalScale::minorPentatonic) { offsets = minorPentatonic; count = 5; }
            else if (scale == MusicalScale::user) { offsets = userScale_.data(); count = userScaleCount_; }
            auto midi = static_cast<int>(value(signalRootMidi));
            const auto degree = static_cast<int>(step % static_cast<std::uint64_t>(count));
            midi += offsets ? offsets[degree] : degree;
            midi = std::min(midi, 127);
            const auto frequency = value(signalReferenceAHz) * std::exp2((midi - 69) / 12.0);
            frameValue_ = waveSample(phase_, waveform);
            if (waveform != static_cast<int>(SignalWaveform::whiteNoise)
                && waveform != static_cast<int>(SignalWaveform::pinkNoise)
                && waveform != static_cast<int>(SignalWaveform::dc))
                phase_ += frequency / spec_.sampleRate;
        }
        else
        {
            const auto loopCycles = transport == GeneratorTransport::loop
                ? static_cast<double>(loopCount) * sweepPhase(1.0) : 0.0;
            const auto heldEndCycles = transport == GeneratorTransport::freeRun && frame_ > frames
                ? static_cast<double>(frame_ - frames) * value(signalEndFrequency) / spec_.sampleRate : 0.0;
            const auto unwrapped = loopCycles + sweepPhase(position) + heldEndCycles;
            frameValue_ = waveSample(unwrapped, waveform);
            phase_ = unwrapped;
        }
    }
    if (mode == SignalMode::fixed && waveform != static_cast<int>(SignalWaveform::whiteNoise)
        && waveform != static_cast<int>(SignalWaveform::pinkNoise)
        && waveform != static_cast<int>(SignalWaveform::dc))
        frameValue_ = waveSample(phase_, waveform);
}
float SignalGeneratorNode::sample(int channel, float) noexcept
{
    const auto waveform = static_cast<int>(value(signalWaveform));
    if (static_cast<SignalMode>(static_cast<int>(value(signalMode))) != SignalMode::fixed
        && static_cast<GeneratorTransport>(static_cast<int>(value(signalTransport))) == GeneratorTransport::oneShot
        && frame_ >= intervalFrames(value(signalDuration), spec_.sampleRate))
        return 0;
    if (value(signalMuted) == 1.0f) return 0;
    float result = frameValue_;
    if (waveform == static_cast<int>(SignalWaveform::whiteNoise))
        result = nextNoise(channel, false);
    else if (waveform == static_cast<int>(SignalWaveform::pinkNoise))
        result = nextNoise(channel, true);
    else if (waveform == static_cast<int>(SignalWaveform::dc))
        result = value(signalDcValue);
    if (value(signalPhaseInvert) == 1.0f) result = -result;
    return result * amplitude(value(signalLevelDb));
}
void SignalGeneratorNode::endFrame() noexcept
{
    const auto mode = static_cast<SignalMode>(static_cast<int>(value(signalMode)));
    const auto waveform = static_cast<int>(value(signalWaveform));
    if (mode == SignalMode::fixed && waveform < static_cast<int>(SignalWaveform::whiteNoise))
    {
        phase_ += value(signalFrequency) / spec_.sampleRate;
        phase_ -= std::floor(phase_);
    }
    ++frame_;
}

ImpulseGeneratorNode::ImpulseGeneratorNode() : GeneratorNode(impulseParameters) {}
bool ImpulseGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{ return parameter == 2 ? integerIn(target, 0, 1) : parameter != 3 || target == 0 || target == 1; }
float ImpulseGeneratorNode::sample(int, float) noexcept
{
    if (value(3) == 1 || (value(2) == 0 && frame_ != 0)) return 0;
    const auto period = intervalFrames(value(1), spec_.sampleRate);
    return frame_ % period == 0 ? amplitude(value(0)) : 0;
}
void ImpulseGeneratorNode::endFrame() noexcept { ++frame_; }

ClickGeneratorNode::ClickGeneratorNode() : GeneratorNode(clickParameters) {}
bool ClickGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{ return parameter == 5 ? integerIn(target, 0, 1) : parameter != 4 || target == 0 || target == 1; }
void ClickGeneratorNode::beginFrame(std::span<const float>) noexcept
{
    if (frame_ < nextClickFrame_) return;
    clickStartFrame_ = frame_;
    const auto seconds = value(5) == 1 ? value(6) : 60.0 / value(0);
    intervalFrames_ = intervalFrames(seconds, spec_.sampleRate);
    nextClickFrame_ = frame_ > std::numeric_limits<std::uint64_t>::max() - intervalFrames_
        ? std::numeric_limits<std::uint64_t>::max() : frame_ + intervalFrames_;
    phase_ = 0;
}
float ClickGeneratorNode::sample(int, float) noexcept
{
    const auto offset = frame_ - clickStartFrame_;
    if (value(4) == 1 || offset >= intervalFrames(value(2), spec_.sampleRate)) return 0;
    const auto duration = std::max(1.0, value(2) * spec_.sampleRate);
    const auto t = static_cast<double>(offset) / spec_.sampleRate;
    const auto envelope = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * t / duration);
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_) * envelope * amplitude(value(3)));
}
void ClickGeneratorNode::endFrame() noexcept
{
    phase_ += value(1) / spec_.sampleRate;
    phase_ -= std::floor(phase_);
    ++frame_;
}

BurstGeneratorNode::BurstGeneratorNode() : GeneratorNode(burstParameters) {}
bool BurstGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{ return parameter != 5 || integerIn(target, 0, 1) ? (parameter != 6 || target == 0 || target == 1) : false; }
void BurstGeneratorNode::beginFrame(std::span<const float>) noexcept
{
    burstFrames_ = intervalFrames(value(1), spec_.sampleRate);
    intervalFrames_ = intervalFrames(value(2), spec_.sampleRate);
}
float BurstGeneratorNode::sample(int, float) noexcept
{
    if (value(6) == 1 || (value(5) == 0 && frame_ >= burstFrames_)) return 0;
    const auto offset = frame_ % intervalFrames_;
    if (offset >= burstFrames_) return 0;
    const auto fadeFrames = std::min(burstFrames_ / 2, intervalFrames(value(4), spec_.sampleRate));
    double envelope = 1;
    if (fadeFrames > 0 && offset < fadeFrames)
        envelope = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(offset) / fadeFrames);
    else if (fadeFrames > 0 && burstFrames_ - offset <= fadeFrames)
        envelope = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(burstFrames_ - offset) / fadeFrames);
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_) * envelope * amplitude(value(3)));
}
void BurstGeneratorNode::endFrame() noexcept
{
    phase_ += value(0) / spec_.sampleRate;
    phase_ -= std::floor(phase_);
    ++frame_;
}
application::Result BurstGeneratorNode::prepare(ProcessingSpec spec)
{
    const auto prepared = GeneratorNode::prepare(spec);
    if (!prepared.succeeded) return prepared;
    if (value(5) == 1.0f && value(2) < value(1))
        return application::Result::failure("Periodic Burst duration must not exceed its interval; overlapping bursts are unsupported.");
    return application::Result::success();
}

ChirpGeneratorNode::ChirpGeneratorNode() : GeneratorNode(chirpParameters) {}
bool ChirpGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{ return parameter == 3 ? integerIn(target, 0, 2) : parameter == 6 ? integerIn(target, 0, 2)
    : parameter != 7 || target == 0 || target == 1; }
void ChirpGeneratorNode::resetState() noexcept { frame_ = 0; }
double ChirpGeneratorNode::phase(double position) const noexcept
{
    const auto duration = static_cast<double>(intervalFrames(value(2), spec_.sampleRate)) / spec_.sampleRate;
    return duration * integratedCurve(value(0), value(1), position, static_cast<int>(value(3)), value(4));
}
float ChirpGeneratorNode::sample(int, float) noexcept
{
    const auto frames = intervalFrames(value(2), spec_.sampleRate);
    const auto transport = static_cast<GeneratorTransport>(static_cast<int>(value(6)));
    if (transport == GeneratorTransport::oneShot && frame_ >= frames) return 0;
    const auto loopCount = transport == GeneratorTransport::loop ? frame_ / frames : 0;
    const auto offset = transport == GeneratorTransport::loop ? frame_ % frames : std::min(frame_, frames);
    const auto position = static_cast<double>(offset) / static_cast<double>(frames);
    const auto loopCycles = transport == GeneratorTransport::loop
        ? static_cast<double>(loopCount) * phase(1.0) : 0.0;
    const auto heldEndCycles = transport == GeneratorTransport::freeRun && frame_ > frames
        ? static_cast<double>(frame_ - frames) * value(1) / spec_.sampleRate : 0.0;
    const auto cycles = loopCycles + phase(position) + heldEndCycles;
    return value(7) == 1 ? 0 : static_cast<float>(std::sin(2.0 * std::numbers::pi * cycles) * amplitude(value(5)));
}
void ChirpGeneratorNode::endFrame() noexcept { ++frame_; }

NoiseGeneratorNode::NoiseGeneratorNode() : GeneratorNode(noiseParameters) {}
bool NoiseGeneratorNode::validTarget(std::uint16_t parameter, float target) const noexcept
{ return parameter == 0 ? integerIn(target, 0, 1) : parameter == 2 ? integerIn(target, 0, 16777215)
    : parameter != 3 || target == 0 || target == 1; }
void NoiseGeneratorNode::resetState() noexcept
{
    const auto seed = static_cast<std::uint64_t>(value(2));
    std::fill(pinkState_.begin(), pinkState_.end(), std::array<float, 7>{});
    for (std::size_t channel = 0; channel < random_.size(); ++channel)
    {
        const auto mixed = (seed + 1) ^ (static_cast<std::uint64_t>(channel + 1) * 0x9e3779b97f4a7c15ULL);
        random_[channel] = mixed == 0 ? 0x2545f4914f6cdd1dULL : mixed;
    }
}
float NoiseGeneratorNode::nextNoise(int channel, bool pink) noexcept
{
    return pink ? pinkSample(random_[static_cast<std::size_t>(channel)], pinkState_[static_cast<std::size_t>(channel)])
                : whiteSample(random_[static_cast<std::size_t>(channel)]);
}
float NoiseGeneratorNode::sample(int channel, float) noexcept
{
    if (value(3) == 1) return 0;
    return nextNoise(channel, value(0) == 1) * amplitude(value(1));
}

DcGeneratorNode::DcGeneratorNode() : GeneratorNode(dcParameters) {}
}
