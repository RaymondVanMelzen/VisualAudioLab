#pragma once

#include <val/audio/AudioNode.h>

namespace val::audio
{
/** Linear [0,4] Gain; the original GainNode now shares smoothing, automation and bypass. */
class GainNode final : public EffectNode
{
public:
    explicit GainNode(float gain = 1, double sampleRate = 48000);
    /** Render-only target update; malformed values return false without modifying the current target. */
    [[nodiscard]] bool setGain(float gain) noexcept;
private:
    float sample(int, float input) noexcept override { return input * value(0); }
};

/** One peaking biquad (Hz, dB, Q), with bounded per-frame RBJ coefficient evaluation. */
class EqNode final : public EffectNode
{
public:
    EqNode();
private:
    void resetState() noexcept override;
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int channel, float input) noexcept override;
    bool validTarget(std::uint16_t parameter, float target) const noexcept override
    { return parameter != 0 || target <= spec_.sampleRate * 0.45; }
    std::array<double, 32> z1_{}, z2_{};
    double b0_{1}, b1_{}, b2_{}, a1_{}, a2_{};
};

/** Fractional feedback delay; maximum two seconds allocated in prepare, independent channel state. */
class DelayNode final : public EffectNode
{
public:
    DelayNode();
    std::size_t requiredStorageBytes(ProcessingSpec spec) const noexcept override;
private:
    void prepareState() override;
    void resetState() noexcept override;
    float sample(int channel, float input) noexcept override;
    void endFrame() noexcept override;
    std::vector<float> memory_;
    std::size_t length_{}, position_{};
};

/** Stereo-linked feed-forward peak compressor; no lookahead or implicit algorithmic latency. */
class CompressorNode final : public EffectNode
{
public:
    CompressorNode();
private:
    void resetState() noexcept override;
    void beginFrame(std::span<const float> inputs) noexcept override;
    float sample(int, float input) noexcept override { return input * gain_; }
    double envelope_{};
    float gain_{1};
};

/** Normalized tanh waveshaper with dry/wet blend; intentionally not oversampled. */
class DistortionNode final : public EffectNode
{
public:
    DistortionNode();
private:
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int, float input) noexcept override;
    float normalization_{1};
};

/** Sine-modulated fractional delay chorus; forty milliseconds allocated per channel. */
class ChorusNode final : public EffectNode
{
public:
    ChorusNode();
    std::size_t requiredStorageBytes(ProcessingSpec spec) const noexcept override;
private:
    void prepareState() override;
    void resetState() noexcept override;
    void beginFrame(std::span<const float>) noexcept override;
    float sample(int channel, float input) noexcept override;
    void endFrame() noexcept override;
    std::vector<float> memory_;
    std::size_t length_{}, position_{};
    double phase_{};
    std::array<double, 32> delay_{};
};

/** Original Schroeder topology: four damped parallel combs, then two serial allpasses per channel. */
class ReverbNode final : public EffectNode
{
public:
    ReverbNode();
    std::size_t requiredStorageBytes(ProcessingSpec spec) const noexcept override;
private:
    /** One independently owned fixed delay line, allocated only during preparation. */
    struct Line { std::vector<float> samples; std::size_t position{}; double lowpass{}; };
    void prepareState() override;
    void resetState() noexcept override;
    float sample(int channel, float input) noexcept override;
    std::array<std::array<Line, 6>, 32> lines_;
};
}
