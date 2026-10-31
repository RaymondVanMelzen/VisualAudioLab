#pragma once

#include <val/audio/AudioProcessorNode.h>
#include <val/application/Result.h>
#include <val/application/IAudioBackend.h>
#include <val/domain/Graph.h>
#include <memory>
#include <vector>

namespace val::audio
{
/** Only the three explicitly supported basic node types; no effects or plugins. */
enum class AudioNodeType { input, gain, output };
/** Control-owned node definition with stable identity and validated initial gain. */
struct AudioNodeDefinition
{
    std::uint64_t id{};
    AudioNodeType type{};
    float gain{1};
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
    void process(const application::AudioBuffer& buffer) noexcept;
    [[nodiscard]] bool gainIndex(std::uint64_t node, std::size_t& index) const noexcept;
    /** Applies a prevalidated gain event to a prepared index on the audio thread. */
    void setGain(std::size_t index, float value) noexcept;
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
    };
    application::AudioStreamFormat format_;
    std::uint64_t revision_{};
    int channels_{};
    std::vector<Step> steps_;
    std::vector<float> scratch_;
};
}
