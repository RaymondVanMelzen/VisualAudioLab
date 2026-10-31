#include <val/audio/AudioGraph.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <new>

namespace val::audio
{
using namespace application;
namespace
{
AudioError invalid(std::string message) { return {AudioErrorCode::invalidArgument, std::move(message)}; }
std::unique_ptr<AudioNode> createProcessor(AudioNodeType type, float gain = 1)
{
    switch (type)
    {
        case AudioNodeType::input: return std::make_unique<InputNode>();
        case AudioNodeType::output: return std::make_unique<OutputNode>();
        case AudioNodeType::gain: return std::make_unique<GainNode>(gain);
        case AudioNodeType::eq: return std::make_unique<EqNode>();
        case AudioNodeType::delay: return std::make_unique<DelayNode>();
        case AudioNodeType::compressor: return std::make_unique<CompressorNode>();
        case AudioNodeType::distortion: return std::make_unique<DistortionNode>();
        case AudioNodeType::chorus: return std::make_unique<ChorusNode>();
        case AudioNodeType::reverb: return std::make_unique<ReverbNode>();
        case AudioNodeType::analyzer: return std::make_unique<AnalyzerNode>();
    }
    return {};
}
}

AudioQuery<AudioGraph> AudioGraph::fromStructural(const domain::Graph& graph, int channels)
{
    AudioGraph result;
    result.channels = channels;
    for (const auto& [id, node] : graph.nodes)
    {
        if (id == 0 || node.id != id)
            return {{}, invalid("Structural node identity is invalid.")};
        const auto field = node.parameters.find("audio.type");
        const auto* type = field != node.parameters.end() ? std::get_if<std::string>(&field->second) : nullptr;
        if (!type) return {{}, invalid("Node " + std::to_string(id) + " needs an explicit documented audio.type.")};
        AudioNodeDefinition definition{id};
        if (*type == "Input") definition.type = AudioNodeType::input;
        else if (*type == "Output") definition.type = AudioNodeType::output;
        else if (*type == "Gain") definition.type = AudioNodeType::gain;
        else if (*type == "EQ") definition.type = AudioNodeType::eq;
        else if (*type == "Delay") definition.type = AudioNodeType::delay;
        else if (*type == "Compressor") definition.type = AudioNodeType::compressor;
        else if (*type == "Distortion") definition.type = AudioNodeType::distortion;
        else if (*type == "Chorus") definition.type = AudioNodeType::chorus;
        else if (*type == "Reverb") definition.type = AudioNodeType::reverb;
        else if (*type == "Analyzer") definition.type = AudioNodeType::analyzer;
        else return {{}, invalid("Unsupported audio.type on node " + std::to_string(id) + ".")};
        if (const auto gain = node.parameters.find("audio.gain"); gain != node.parameters.end())
        {
            const auto* value = std::get_if<double>(&gain->second);
            if (definition.type != AudioNodeType::gain || !value || !std::isfinite(*value) || *value < 0 || *value > 4)
                return {{}, invalid("audio.gain requires a finite Gain-node linear value from 0 to 4.")};
            definition.gain = static_cast<float>(*value);
        }
        for (const auto& [key, scalar] : node.parameters)
        {
            if (key == "audio.bypass")
            {
                const auto* value = std::get_if<bool>(&scalar);
                if (!value) return {{}, invalid("audio.bypass must be boolean.")};
                definition.bypassed = *value;
            }
            if (key.starts_with("audio.param."))
            {
                const auto name = key.substr(12);
                const auto* value = std::get_if<double>(&scalar);
                if (!value || !std::isfinite(*value)) return {{}, invalid("audio.param values must be finite numbers.")};
                // Parameter identities are names in projects and immutable indices in prepared plans.
                const auto prototype = createProcessor(definition.type);
                const auto parameters = prototype->parameters();
                const auto found = std::find_if(parameters.begin(), parameters.end(), [&](const auto& item) { return name == item.name; });
                if (found == parameters.end() || *value < found->minimum || *value > found->maximum)
                    return {{}, invalid("Unknown/out-of-range audio.param." + name)};
                if (definition.type == AudioNodeType::gain && node.parameters.contains("audio.gain"))
                    return {{}, invalid("Use audio.gain or audio.param.gain, not both.")};
                definition.parameters.emplace_back(static_cast<std::uint16_t>(found - parameters.begin()), static_cast<float>(*value));
            }
        }
        result.nodes.push_back(definition);
    }
    for (const auto& [id, edge] : graph.connections)
    {
        if (id == 0 || edge.id != id || edge.source.port != domain::Port::output
            || edge.destination.port != domain::Port::input)
            return {{}, invalid("Structural connection identity/direction is invalid.")};
        result.connections.push_back({edge.source.node, edge.destination.node});
    }
    return {std::move(result), {}};
}

AudioQuery<std::unique_ptr<PreparedAudioGraph>> PreparedAudioGraph::prepare(
    const AudioGraph& graph, AudioStreamFormat format, std::uint64_t revision) try
{
    if (!std::isfinite(format.sampleRate) || format.sampleRate < 8000 || format.sampleRate > 384000
        || format.maxFrames < 1 || format.maxFrames > 4096 || format.bufferFrames < 1
        || format.bufferFrames > format.maxFrames || format.inputChannels < 0 || format.inputChannels > 32
        || format.outputChannels < 1 || format.outputChannels > 32 || graph.channels != format.outputChannels
        || (format.inputChannels != 0 && format.inputChannels != graph.channels) || revision == 0)
        return {{}, invalid("Require 8-384 kHz, 1-4096 frame capacity, 1-32 matching bus channels and a positive revision; input is zero or matches output.")};
    if (graph.nodes.size() > 256 || graph.connections.size() > 256)
        return {{}, invalid("Basic runtime limit is 256 nodes/connections.")};
    const auto stride = static_cast<std::size_t>(format.maxFrames) * static_cast<std::size_t>(graph.channels);
    if ((graph.nodes.size() + 1) * stride * sizeof(float) > 64 * 1024 * 1024)
        return {{}, invalid("Prepared scratch exceeds the 64 MiB runtime budget.")};

    std::map<std::uint64_t, const AudioNodeDefinition*> nodes;
    std::map<std::uint64_t, std::uint64_t> incoming;
    std::map<std::uint64_t, std::vector<std::uint64_t>> outgoing;
    std::uint64_t output{};
    for (const auto& node : graph.nodes)
    {
        if (node.id == 0 || !nodes.emplace(node.id, &node).second || !std::isfinite(node.gain)
            || node.gain < 0 || node.gain > 4)
            return {{}, invalid("Duplicate/zero node ID or invalid gain (finite 0-4 required).")};
        if (node.type < AudioNodeType::input || node.type > AudioNodeType::analyzer)
            return {{}, invalid("Unsupported processor type.")};
        if (node.type == AudioNodeType::output)
        {
            if (output) return {{}, invalid("Only one explicit Output bus is supported; no implicit mixing.")};
            output = node.id;
        }
    }
    if (!nodes.empty() && output == 0)
        return {{}, invalid("A nonempty runtime graph requires one Output node.")};
    for (const auto& edge : graph.connections)
    {
        if (!nodes.contains(edge.source) || !nodes.contains(edge.destination) || edge.source == edge.destination
            || nodes.at(edge.source)->type == AudioNodeType::output || nodes.at(edge.destination)->type == AudioNodeType::input
            || !incoming.emplace(edge.destination, edge.source).second)
            return {{}, invalid("Invalid/missing endpoint, direction, self-link or multiple input sources.")};
        outgoing[edge.source].push_back(edge.destination);
    }
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<>> ready;
    for (const auto& [id, node] : nodes)
        if (!incoming.contains(id)) ready.push(id);
    std::vector<std::uint64_t> order;
    while (!ready.empty())
    {
        const auto id = ready.top(); ready.pop();
        order.push_back(id);
        for (const auto next : outgoing[id]) ready.push(next); // At most one upstream per input.
    }
    if (order.size() != nodes.size()) return {{}, invalid("Runtime graph contains a directed cycle.")};
    std::set<std::uint64_t> reachable;
    for (auto id = output; id != 0;)
    {
        reachable.insert(id);
        const auto found = incoming.find(id);
        id = found == incoming.end() ? 0 : found->second;
    }
    if (reachable.size() != nodes.size())
        return {{}, invalid("Every basic runtime node must feed the Output; disconnected/dead branches are rejected.")};

    auto plan = std::unique_ptr<PreparedAudioGraph>(new PreparedAudioGraph);
    plan->format_ = format;
    plan->revision_ = revision;
    plan->channels_ = graph.channels;
    plan->scratch_.resize((nodes.size() + 1) * stride, 0.0f);
    std::size_t storageBytes = plan->scratch_.size() * sizeof(float);
    std::map<std::uint64_t, std::size_t> offsets;
    for (auto id : order)
    {
        const auto& node = *nodes.at(id);
        const auto destination = (plan->steps_.size() + 1) * stride;
        const auto source = incoming.contains(id) ? offsets.at(incoming.at(id)) : 0;
        auto processor = createProcessor(node.type, node.gain);
        const ProcessingSpec spec{format.sampleRate, format.maxFrames, graph.channels};
        storageBytes += processor->requiredStorageBytes(spec);
        if (storageBytes > 128 * 1024 * 1024)
            return {{}, invalid("Prepared DSP storage exceeds the 128 MiB graph budget.")};
        std::set<std::uint16_t> assigned;
        for (const auto& [parameter, value] : node.parameters)
            if (!assigned.insert(parameter).second || !processor->initialParameter(parameter, value))
                return {{}, invalid("Duplicate, unknown or out-of-range initial processor parameter.")};
        if (node.bypassed && (node.type == AudioNodeType::input || node.type == AudioNodeType::output))
            return {{}, invalid("Device routing nodes do not support bypass.")};
        processor->initialBypass(node.bypassed);
        if (const auto prepared = processor->prepare(spec); !prepared.succeeded)
            return {{}, invalid(prepared.error)};
        plan->steps_.push_back({id, node.type, std::move(processor), source, destination});
        offsets.emplace(id, destination);
    }
    return {std::move(plan), {}};
}
catch (const std::bad_alloc&)
{
    return {{}, {AudioErrorCode::unavailable, "Insufficient memory while preparing the audio graph; active plan was not replaced."}};
}

void PreparedAudioGraph::process(const AudioBuffer& buffer, int offset, int frames) noexcept
{
    if (frames < 0) frames = buffer.frames;
    for (auto& step : steps_)
        step.processor->process({scratch_.data() + step.source + offset, scratch_.data() + step.destination + offset,
                                 channels_, format_.maxFrames, frames, buffer, offset});
}
bool PreparedAudioGraph::parameterIndex(std::uint64_t node, std::uint16_t parameter, float value,
                                        ParameterAction action, std::size_t& index) const noexcept
{
    for (std::size_t i = 0; i < steps_.size(); ++i)
        if (steps_[i].id == node && steps_[i].type != AudioNodeType::input && steps_[i].type != AudioNodeType::output
            && steps_[i].processor->accepts(parameter, value, action)) { index = i; return true; }
    return false;
}
void PreparedAudioGraph::parameterEvent(std::size_t index, std::uint16_t parameter, float value, ParameterAction action) noexcept
{
    const auto accepted = steps_[index].processor->parameterEvent(parameter, value, action);
    (void) accepted;
}

bool PreparedAudioGraph::gainIndex(std::uint64_t node, std::size_t& index) const noexcept
{
    for (std::size_t i = 0; i < steps_.size(); ++i)
        if (steps_[i].id == node && steps_[i].type == AudioNodeType::gain) { index = i; return true; }
    return false;
}

void PreparedAudioGraph::setGain(std::size_t index, float value) noexcept
{
    const auto accepted = static_cast<GainNode*>(steps_[index].processor.get())->setGain(value);
    (void) accepted; // Caller supplies a prevalidated prepared index/value.
}
std::optional<AnalyzerSnapshot> PreparedAudioGraph::analyzerSnapshot(std::uint64_t node) const noexcept
{
    for (const auto& step : steps_)
        if (step.id == node && step.type == AudioNodeType::analyzer)
            return static_cast<const AnalyzerNode*>(step.processor.get())->snapshot();
    return std::nullopt;
}
std::uint64_t PreparedAudioGraph::numericalFaults() const noexcept
{
    std::uint64_t faults{};
    for (const auto& step : steps_) faults += step.processor->numericalFaults();
    return faults;
}
}
