#pragma once

#include <val/audio/AudioNode.h>
#include <array>
#include <string_view>
#include <vector>

namespace val::audio
{
enum class SignalWaveform : int { sine, square, triangle, sawtooth, whiteNoise, pinkNoise, dc };
enum class SignalMode : int { fixed, sweep, musical };
enum class SweepCurve : int { linear, logarithmic, exponential };
enum class GeneratorTransport : int { freeRun, oneShot, loop };
enum class MusicalScale : int { chromatic, major, naturalMinor, majorPentatonic, minorPentatonic, user };

enum SignalGeneratorParameter : std::uint16_t
{
    signalWaveform, signalFrequency, signalMode, signalStartFrequency, signalEndFrequency,
    signalDuration, signalSweepCurve, signalCurvature, signalStepSeconds, signalScale,
    signalRootMidi, signalLevelDb, signalMuted, signalPhaseInvert, signalTransport, signalDcValue,
    signalReferenceAHz
};

struct FrequencyEntryResult
{
    double hertz{};
    std::string_view error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

/** Parses an explicit Hz, MIDI, or named-note value without guessing ambiguous forms. */
[[nodiscard]] FrequencyEntryResult parseFrequencyEntry(std::string_view text, double referenceAHz) noexcept;

struct UserScaleResult
{
    std::array<int, 12> offsets{};
    std::size_t count{};
    std::string_view error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

/** Parses a bounded comma-separated list of ascending, unique semitone offsets. */
[[nodiscard]] UserScaleResult parseUserScale(std::string_view text) noexcept;

/** Multi-waveform source. Discrete enum parameters are integer-valued and sample-timestamped. */
class SignalGeneratorNode final : public GeneratorNode
{
public:
    SignalGeneratorNode();
    /** Control-thread configuration before prepare; offsets must be validated, ordered semitones. */
    [[nodiscard]] bool setUserScale(std::span<const int> offsets) noexcept;
private:
    void prepareState() override;
    void resetState() noexcept override;
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int channel, float) noexcept override;
    void endFrame() noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    [[nodiscard]] double sweepPhase(double position) const noexcept;
    [[nodiscard]] float nextNoise(int channel, bool pink) noexcept;
    std::array<std::uint64_t, 32> random_{};
    std::array<std::array<float, 7>, 32> pinkState_{};
    std::array<int, 12> userScale_{};
    int userScaleCount_{};
    std::uint64_t frame_{};
    double phase_{};
    float frameValue_{};
};

/** Emits a single sample at frame zero or each prepared interval. */
class ImpulseGeneratorNode final : public GeneratorNode
{
public:
    ImpulseGeneratorNode();
private:
    void resetState() noexcept override { frame_ = 0; }
    float sample(int, float) noexcept override;
    void endFrame() noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    std::uint64_t frame_{}, intervalFrames_{};
};

/** BPM-synchronized, short decaying tone click; distinct from a one-sample impulse. */
class ClickGeneratorNode final : public GeneratorNode
{
public:
    ClickGeneratorNode();
private:
    void resetState() noexcept override { frame_ = 0; clickStartFrame_ = 0; nextClickFrame_ = 0; phase_ = 0; }
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int, float) noexcept override;
    void endFrame() noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    std::uint64_t frame_{}, clickStartFrame_{}, nextClickFrame_{}, intervalFrames_{};
    double phase_{};
};

/** Finite sine burst with raised-cosine edge shaping. */
class BurstGeneratorNode final : public GeneratorNode
{
public:
    BurstGeneratorNode();
    [[nodiscard]] application::Result prepare(ProcessingSpec spec) override;
private:
    void resetState() noexcept override { frame_ = 0; phase_ = 0; }
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int, float) noexcept override;
    void endFrame() noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    std::uint64_t frame_{}, burstFrames_{}, intervalFrames_{};
    double phase_{};
};

/** Dedicated swept-sine source; frequency curves share SignalGenerator definitions. */
class ChirpGeneratorNode final : public GeneratorNode
{
public:
    ChirpGeneratorNode();
private:
    void resetState() noexcept override;
    float sample(int, float) noexcept override;
    void endFrame() noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    [[nodiscard]] double phase(double position) const noexcept;
    std::uint64_t frame_{};
};

/** Independent deterministic per-channel white or bounded filtered-pink noise. */
class NoiseGeneratorNode final : public GeneratorNode
{
public:
    NoiseGeneratorNode();
private:
    void resetState() noexcept override;
    float sample(int channel, float) noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override;
    [[nodiscard]] float nextNoise(int channel, bool pink) noexcept;
    std::array<std::uint64_t, 32> random_{};
    std::array<std::array<float, 7>, 32> pinkState_{};
};

/** Exact constant source in [-1, +1], with no implicit dB conversion. */
class DcGeneratorNode final : public GeneratorNode
{
public:
    DcGeneratorNode();
private:
    float sample(int, float) noexcept override { return value(0) * (value(1) == 1 ? 0.0f : 1.0f); }
    bool validTarget(std::uint16_t parameter, float target) const noexcept override
    { return parameter != 1 || (target == 0.0f || target == 1.0f); }
};
}
