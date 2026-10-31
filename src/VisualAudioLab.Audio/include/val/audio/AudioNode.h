#pragma once

#include <val/application/AudioStream.h>
#include <val/application/Result.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace val::audio
{
/** Immutable render limits. Reprepare only after rendering is quiescent. */
struct ProcessingSpec { double sampleRate{48000}; int maxFrames{512}, channels{2}; };

/** Borrowed planar scratch span; hardwareOffset locates a split span in the original host block. */
struct NodeProcessContext
{
    const float* source{};
    float* destination{};
    int channels{}, stride{}, frames{};
    const application::AudioBuffer& hardware;
    int hardwareOffset{};
};

/** Stable physical-domain parameter metadata; linear target ramps have sample-time duration. */
struct ParameterSpec
{
    const char* name{};
    float minimum{}, maximum{}, initial{};
    double smoothingSeconds{};
};

/** Ordered parameter-layer transitions; automation targets are steps followed by declared smoothing. */
enum class ParameterAction { base, automation, overrideValue, releaseOverride, disableAutomation };

/** Deterministic linear sample smoother; inputs must be finite/validated, durations 0-1 s and rates 8-384 kHz. */
class ParameterSmoother final
{
public:
    void prepare(double sampleRate, double seconds) noexcept;
    void reset(float value) noexcept;
    void target(float value) noexcept;
    [[nodiscard]] float next() noexcept;
    [[nodiscard]] float current() const noexcept { return current_; }
private:
    float current_{}, target_{}, step_{};
    int duration_{}, remaining_{};
};

/**
 * Unified prepared DSP node. AudioProcessorNode is the legacy-name alias, not a second hierarchy.
 * prepare/initialization/destruction are control-only; reset requires quiescence or an audio boundary.
 * process and parameter events are serial render-only and bounded by the prepared spec.
 */
class AudioNode
{
public:
    static constexpr std::uint16_t bypassParameter = 255;
    virtual ~AudioNode() = default;
    [[nodiscard]] virtual application::Result prepare(ProcessingSpec spec);
    virtual void reset() noexcept;
    virtual void process(const NodeProcessContext& context) noexcept;
    [[nodiscard]] std::span<const ParameterSpec> parameters() const noexcept { return definitions_; }
    /** Control validation reads only immutable definitions, never live render state. */
    [[nodiscard]] bool accepts(std::uint16_t parameter, float value, ParameterAction action) const noexcept;
    /** Render-only, prevalidated event. False is an explicit malformed-event outcome. */
    [[nodiscard]] bool parameterEvent(std::uint16_t parameter, float value, ParameterAction action) noexcept;
    /** Set persisted base values before prepare, never concurrently with rendering. */
    [[nodiscard]] bool initialParameter(std::uint16_t parameter, float value) noexcept;
    void initialBypass(bool bypassed) noexcept { initialBypass_ = bypassed; }
    [[nodiscard]] virtual int latencyFrames() const noexcept { return 0; }
    [[nodiscard]] virtual std::size_t requiredStorageBytes(ProcessingSpec spec) const noexcept;
    [[nodiscard]] std::uint64_t numericalFaults() const noexcept { return faults_.load(std::memory_order_relaxed); }
protected:
    explicit AudioNode(std::span<const ParameterSpec> parameters = {});
    virtual void prepareState() {}
    virtual void resetState() noexcept {}
    virtual void beginFrame(std::span<const float>) noexcept {}
    virtual void endFrame() noexcept {}
    [[nodiscard]] virtual float sample(int channel, float input) noexcept = 0;
    [[nodiscard]] virtual bool usesInput() const noexcept { return true; }
    [[nodiscard]] virtual bool validTarget(std::uint16_t, float) const noexcept { return true; }
    [[nodiscard]] float value(std::size_t index) const noexcept { return values_[index]; }
    [[nodiscard]] float safe(float value) noexcept;
    [[nodiscard]] double cleanState(double value) noexcept;
    ProcessingSpec spec_;
private:
    /** Render-owned layers preserve saved base values while automation and live override are active. */
    struct ParameterState
    {
        float base{}, automated{}, overridden{};
        bool automation{}, overrideActive{};
        ParameterSmoother smoother;
        [[nodiscard]] float selected() const noexcept
        { return overrideActive ? overridden : automation ? automated : base; }
    };
    std::span<const ParameterSpec> definitions_;
    std::array<ParameterState, 8> states_{};
    ParameterState bypass_;
    std::array<float, 8> initial_{}, values_{};
    ParameterSmoother wet_;
    bool initialBypass_{}, prepared_{};
    std::vector<float> dryDelay_;
    std::size_t dryPosition_{};
    std::atomic<std::uint64_t> faults_{};
};

/** Audio-to-audio processor; bypass crossfades against latency-aligned dry samples. */
class EffectNode : public AudioNode
{
protected:
    explicit EffectNode(std::span<const ParameterSpec> parameters = {}) : AudioNode(parameters) {}
};

/** Audio source base: no hidden input route; bypass fades the generator to silence. */
class GeneratorNode : public AudioNode
{
protected:
    explicit GeneratorNode(std::span<const ParameterSpec> parameters = {}) : AudioNode(parameters) {}
    bool usesInput() const noexcept final { return false; }
};

/** Approximate lock-free observation of the last completed 256-frame analysis window. */
struct AnalyzerSnapshot
{
    std::array<float, 32> peak{}, rms{};
    std::uint64_t windows{};
};

/**
 * Pass-through analysis base with independent 256-sample peak/RMS windows.
 * Derived analyzers may extend beginFrame/endFrame but must retain bounded, allocation-free work.
 * Snapshot fields are independent atomic observations, not a coherent multi-field transaction.
 */
class AnalyzerNode : public EffectNode
{
public:
    AnalyzerNode() = default;
    [[nodiscard]] AnalyzerSnapshot snapshot() const noexcept;
protected:
    void resetState() noexcept override;
    float sample(int channel, float input) noexcept override;
    void endFrame() noexcept override;
private:
    std::array<float, 32> peak_{};
    std::array<double, 32> squares_{};
    std::array<std::atomic<std::uint32_t>, 32> publishedPeak_{}, publishedRms_{};
    std::atomic<std::uint64_t> windows_{};
    int frames_{};
};
}
