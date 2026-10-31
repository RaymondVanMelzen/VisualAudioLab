#pragma once

#include <val/audio/AudioProcessorNode.h>
#include <val/application/Result.h>
#include <val/application/IAudioBackend.h>
#include <val/domain/Graph.h>
#include <val/script/Program.h>
#include <memory>
#include <vector>

namespace val::audio
{
/** Built-in prepared processors; no plugin hosting or implicit processor-name inference. */
enum class AudioNodeType { input, gain, output, eq, delay, compressor, distortion, chorus, reverb, analyzer, script };
/** Control-owned node definition with stable identity, initial physical-domain parameters and bypass. */
struct AudioNodeDefinition
{
    std::uint64_t id{};
    AudioNodeType type{};
    float gain{1};
    std::vector<std::pair<std::uint16_t, float>> parameters;
    bool bypassed{};
    /** Optional already-admitted observation transport; control retains ownership until callback retirement. */
    std::shared_ptr<SignalCaptureBuffer> capture;
    /** Immutable compiler-produced artifact; no parsing/compilation is allowed during processing. */
    std::shared_ptr<const script::Program> script;
};
/** Single-bus graph edge; fan-out allowed, implicit fan-in/mixing forbidden. */
struct AudioConnection
{
    std::uint64_t source{}, destination{};
};
/** Control-owned graph description, independent of canvas geometry and device objects. */
struct AudioGraph
{
    int channels{2};
    std::vector<AudioNodeDefinition> nodes;
    std::vector<AudioConnection> connections;
    /** Explicit opt-in mapping via audio.type parameters; generic editor nodes are rejected. */
    [[nodiscard]] static application::AudioQuery<AudioGraph> fromStructural(const domain::Graph& graph, int channels);
};

/** Validated execution plan, with all nodes/scratch allocated before publication. */
class PreparedAudioGraph final
{
public:
    /** Returns detailed validation errors without changing an active plan. */
    [[nodiscard]] static application::AudioQuery<std::unique_ptr<PreparedAudioGraph>>
        prepare(const AudioGraph& graph, application::AudioStreamFormat format, std::uint64_t revision);
    /** Audio thread only, bounded by the prepared resource limits; no allocations/ownership changes. */
    void process(const application::AudioBuffer& buffer, int offset = 0, int frames = -1) noexcept;
    /** Copy complete node outputs once per host block, after all sample-offset parameter spans. */
    void capture(int frames, std::uint64_t epoch, std::uint64_t firstFrame) noexcept;
    [[nodiscard]] bool gainIndex(std::uint64_t node, std::size_t& index) const noexcept;
    /** Applies a prevalidated gain event to a prepared index on the audio thread. */
    void setGain(std::size_t index, float value) noexcept;
    [[nodiscard]] bool parameterIndex(std::uint64_t node, std::uint16_t parameter, float value,
                                     ParameterAction action, std::size_t& index) const noexcept;
    void parameterEvent(std::size_t index, std::uint16_t parameter, float value, ParameterAction action) noexcept;
    [[nodiscard]] std::optional<AnalyzerSnapshot> analyzerSnapshot(std::uint64_t node) const noexcept;
    [[nodiscard]] std::optional<script::RuntimeFault> scriptFault(std::uint64_t node) const noexcept;
    [[nodiscard]] std::uint64_t numericalFaults() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] application::AudioStreamFormat format() const noexcept { return format_; }
private:
    /** One topologically ordered, independently owned prepared processor and its upstream scratch offset. */
    struct Step
    {
        std::uint64_t id{};
        AudioNodeType type{};
        std::unique_ptr<AudioProcessorNode> processor;
        std::size_t source{}, destination{};
        std::shared_ptr<SignalCaptureBuffer> capture;
    };
    application::AudioStreamFormat format_;
    std::uint64_t revision_{};
    int channels_{};
    std::vector<Step> steps_;
    std::vector<float> scratch_;
};
}
