#include <val/audio/AudioGraph.h>
#include <val/audio/GeneratorNodes.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string_view>
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
        case AudioNodeType::signalGenerator: return std::make_unique<SignalGeneratorNode>();
        case AudioNodeType::impulseGenerator: return std::make_unique<ImpulseGeneratorNode>();
        case AudioNodeType::clickGenerator: return std::make_unique<ClickGeneratorNode>();
        case AudioNodeType::burstGenerator: return std::make_unique<BurstGeneratorNode>();
        case AudioNodeType::chirpGenerator: return std::make_unique<ChirpGeneratorNode>();
        case AudioNodeType::noiseGenerator: return std::make_unique<NoiseGeneratorNode>();
        case AudioNodeType::dcGenerator: return std::make_unique<DcGeneratorNode>();
        case AudioNodeType::sum: return std::make_unique<SumNode>();
    }
    return {};
}
bool isGenerator(AudioNodeType type) noexcept
{
    return type == AudioNodeType::signalGenerator || type == AudioNodeType::impulseGenerator
        || type == AudioNodeType::clickGenerator || type == AudioNodeType::burstGenerator
        || type == AudioNodeType::chirpGenerator || type == AudioNodeType::noiseGenerator
        || type == AudioNodeType::dcGenerator;
}
}

AudioQuery<AudioGraph> AudioGraph::fromStructural(const domain::Graph& graph, int channels,
    const std::map<std::uint64_t, std::vector<int>>& endpointChannelMaps)
{
    AudioGraph result;
    result.channels = channels;
    for (const auto& [id, endpoint] : graph.endpoints)
    {
        if (id == 0 || endpoint.id != id)
            return {{}, invalid("Hardware endpoint identity is invalid.")};
        AudioNodeDefinition definition;
        definition.id = id;
        definition.type = endpoint.role == domain::EndpointRole::hardwareInput
            ? AudioNodeType::input : AudioNodeType::output;
        definition.channels = endpoint.layoutChannels;
        definition.hardwareEndpoint = true;
        if (const auto mapping = endpointChannelMaps.find(id); mapping != endpointChannelMaps.end())
        {
            if (!endpoint.channelNames.empty()
                && mapping->second.size() != static_cast<std::size_t>(endpoint.layoutChannels))
                return {{}, invalid("Resolved endpoint channel map must match the saved bus width.")};
            definition.hardwareChannelMap = mapping->second;
            definition.endpointActive = endpoint.enabled;
        }
        else definition.endpointActive = false;
        if (endpoint.layoutChannels < 1 || endpoint.layoutChannels > 32
            || (!endpoint.channelNames.empty()
                && static_cast<int>(endpoint.channelNames.size()) != endpoint.layoutChannels))
            return {{}, invalid("Hardware endpoint has an invalid bus layout or channel assignment.")};
        if (endpoint.channelNames.empty() && definition.endpointActive)
            definition.channels = static_cast<int>(definition.hardwareChannelMap.size());
        result.nodes.push_back(std::move(definition));
    }
    for (const auto& [id, node] : graph.nodes)
    {
        if (id == 0 || node.id != id)
            return {{}, invalid("Structural node identity is invalid.")};
        const auto field = node.parameters.find("audio.type");
        const auto* type = field != node.parameters.end() ? std::get_if<std::string>(&field->second) : nullptr;
        if (!type) return {{}, invalid("Node " + std::to_string(id) + " needs an explicit documented audio.type.")};
        if (*type == "Script")
            return {{}, invalid("VAL Script was retired. This legacy node is preserved in the project but cannot be executed; replace it with a supported module or develop DSP externally.")};
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
        else if (*type == "Signal Generator") definition.type = AudioNodeType::signalGenerator;
        else if (*type == "Impulse Generator") definition.type = AudioNodeType::impulseGenerator;
        else if (*type == "Click Generator") definition.type = AudioNodeType::clickGenerator;
        else if (*type == "Burst Generator") definition.type = AudioNodeType::burstGenerator;
        else if (*type == "Chirp Generator") definition.type = AudioNodeType::chirpGenerator;
        else if (*type == "Noise Generator") definition.type = AudioNodeType::noiseGenerator;
        else if (*type == "DC Generator") definition.type = AudioNodeType::dcGenerator;
        else if (*type == "Sum") definition.type = AudioNodeType::sum;
        else return {{}, invalid("Unsupported audio.type on node " + std::to_string(id) + ".")};
        if (isGenerator(definition.type))
            definition.channels = channels;
        if ((definition.type == AudioNodeType::input || definition.type == AudioNodeType::output)
            && !definition.hardwareEndpoint)
            definition.channels = channels;
        if (const auto bus = node.parameters.find("audio.busChannels"); bus != node.parameters.end())
        {
            const auto* value = std::get_if<double>(&bus->second);
            if (!isGenerator(definition.type) || !value || std::floor(*value) != *value || *value < 1 || *value > 32)
                return {{}, invalid("audio.busChannels is a whole 1-32 channel layout setting for generators only.")};
            definition.channels = static_cast<int>(*value);
        }
        if (const auto gain = node.parameters.find("audio.gain"); gain != node.parameters.end())
        {
            const auto* value = std::get_if<double>(&gain->second);
            if (definition.type != AudioNodeType::gain || !value || !std::isfinite(*value) || *value < 0 || *value > 4)
                return {{}, invalid("audio.gain requires a finite Gain-node linear value from 0 to 4.")};
            definition.gain = static_cast<float>(*value);
        }
        if (const auto customScale = node.parameters.find("audio.scaleIntervals"); customScale != node.parameters.end())
        {
            const auto* value = std::get_if<std::string>(&customScale->second);
            if (definition.type != AudioNodeType::signalGenerator || !value)
                return {{}, invalid("audio.scaleIntervals is a comma-separated ascending semitone list (1-12 values in [0,127]) for Signal Generator User Scale.")};
            const auto parsed = parseUserScale(*value);
            if (!parsed) return {{}, invalid("Invalid audio.scaleIntervals: " + std::string(parsed.error))};
            definition.userScale.assign(parsed.offsets.begin(), parsed.offsets.begin() + static_cast<std::ptrdiff_t>(parsed.count));
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
                auto name = key.substr(12);
                const auto* value = std::get_if<double>(&scalar);
                if (!value || !std::isfinite(*value)) return {{}, invalid("audio.param values must be finite numbers.")};
                // Parameter identities are names in projects and immutable indices in prepared plans.
                const auto prototype = createProcessor(definition.type);
                if (!definition.userScale.empty())
                {
                    auto* signal = dynamic_cast<SignalGeneratorNode*>(prototype.get());
                    if (!signal || !signal->setUserScale(definition.userScale))
                        return {{}, invalid("Signal Generator custom scale configuration is invalid.")};
                }
                const auto parameters = prototype->parameters();
                const auto found = std::find_if(parameters.begin(), parameters.end(), [&](const auto& item) { return name == item.name; });
                if (found == parameters.end()
                    || !prototype->accepts(static_cast<std::uint16_t>(found - parameters.begin()),
                                           static_cast<float>(*value), ParameterAction::base))
                    return {{}, invalid("Unknown/out-of-range audio.param." + name)};
                if (definition.type == AudioNodeType::gain && node.parameters.contains("audio.gain"))
                    return {{}, invalid("Use audio.gain or audio.param.gain, not both.")};
                definition.parameters.emplace_back(static_cast<std::uint16_t>(found - parameters.begin()), static_cast<float>(*value));
            }
        }
        if (definition.type == AudioNodeType::signalGenerator)
        {
            const auto scale = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                [](const auto& parameter) { return parameter.first == signalScale; });
            const auto root = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                [](const auto& parameter) { return parameter.first == signalRootMidi; });
            const auto scaleValue = scale == definition.parameters.end()
                ? 0 : static_cast<int>(scale->second);
            const auto rootValue = root == definition.parameters.end()
                ? 36 : static_cast<int>(root->second);
            if (scaleValue == static_cast<int>(MusicalScale::user)
                && (definition.userScale.empty() || rootValue + definition.userScale.back() > 127))
                return {{}, invalid("Signal Generator User Scale requires audio.scaleIntervals and every root-plus-offset MIDI note must be <= 127.")};
        }
        std::map<std::string, std::map<std::string, const domain::Parameter*>> controlFields;
        constexpr std::string_view controlPrefix{"audio.control."};
        for (const auto& [key, value] : node.parameters)
            if (key.starts_with(controlPrefix))
            {
                const auto suffix = std::string_view(key).substr(controlPrefix.size());
                const auto separator = suffix.find('.');
                if (separator == std::string_view::npos || separator == 0 || separator + 1 == suffix.size())
                    return {{}, invalid("Control mappings use audio.control.<parameter>.<field>.")};
                const auto parameter = std::string(suffix.substr(0, separator));
                const auto fieldName = std::string(suffix.substr(separator + 1));
                constexpr std::string_view supportedFields[]{
                    "source", "offset", "depth", "rateHz", "attackSeconds", "holdSeconds",
                    "releaseSeconds", "minimum", "maximum", "script"};
                if (std::find(std::begin(supportedFields), std::end(supportedFields), fieldName)
                    == std::end(supportedFields))
                    return {{}, invalid("Unsupported control mapping field: " + fieldName + ".")};
                if (!controlFields[parameter].emplace(fieldName, &value).second)
                    return {{}, invalid("Duplicate control mapping field.")};
            }
        if (!controlFields.empty())
        {
            auto prototype = createProcessor(definition.type);
            if (!prototype) return {{}, invalid("Cannot map a parameter on this processor.")};
            if (!definition.userScale.empty())
            {
                auto* signal = dynamic_cast<SignalGeneratorNode*>(prototype.get());
                if (!signal || !signal->setUserScale(definition.userScale))
                    return {{}, invalid("Signal Generator custom scale configuration is invalid.")};
            }
            for (const auto& [parameter, value] : definition.parameters)
                if (!prototype->initialParameter(parameter, value))
                    return {{}, invalid("Control target has an invalid saved base parameter.")};
            const auto specs = prototype->parameters();
            for (const auto& [parameterName, fields] : controlFields)
            {
                const auto target = std::find_if(specs.begin(), specs.end(),
                    [&](const auto& item) { return parameterName == item.name; });
                if (target == specs.end())
                    return {{}, invalid("Unknown control target parameter: " + parameterName + ".")};
                const auto index = static_cast<std::uint16_t>(target - specs.begin());
                const auto getString = [&](std::string_view field) -> const std::string*
                {
                    const auto found = fields.find(std::string(field));
                    return found == fields.end() ? nullptr : std::get_if<std::string>(found->second);
                };
                const auto getNumber = [&](std::string_view field, float& output) -> bool
                {
                    const auto found = fields.find(std::string(field));
                    if (found == fields.end()) return false;
                    const auto* number = std::get_if<double>(found->second);
                    if (!number || !std::isfinite(*number)
                        || *number < -std::numeric_limits<float>::max()
                        || *number > std::numeric_limits<float>::max()) return false;
                    output = static_cast<float>(*number);
                    return true;
                };
                const auto* source = getString("source");
                ParameterControlBinding binding;
                binding.parameter = index;
                if (source && *source == "none") continue;
                if (source && *source == "script")
                    return {{}, invalid("VAL Script scalar control mappings were retired. Their saved source and parameters are preserved but cannot be executed.")};
                if (!source || !getNumber("offset", binding.offset) || !getNumber("depth", binding.depth))
                    return {{}, invalid("Each control mapping requires a source, finite offset and finite depth.")};
                if (*source == "lfo")
                {
                    binding.source = ParameterControlSource::lfo;
                    if (!getNumber("rateHz", binding.rateHz))
                        return {{}, invalid("LFO control requires rateHz from 0.01 to 20.")};
                }
                else if (*source == "envelope")
                {
                    binding.source = ParameterControlSource::envelope;
                    binding.sourceMinimum = 0;
                    binding.sourceMaximum = 1;
                    if (!getNumber("attackSeconds", binding.attackSeconds)
                        || !getNumber("holdSeconds", binding.holdSeconds)
                        || !getNumber("releaseSeconds", binding.releaseSeconds))
                        return {{}, invalid("Envelope control requires attackSeconds, holdSeconds and releaseSeconds.")};
                }
                else return {{}, invalid("Control source must be lfo or envelope.")};
                if (!prototype->initialControl(binding))
                    return {{}, invalid("Control mapping is invalid, out of range, or targets a discrete parameter.")};
                definition.controls.push_back(std::move(binding));
            }
        }
        result.nodes.push_back(definition);
    }
    for (const auto& [id, edge] : graph.connections)
    {
        if (id == 0 || edge.id != id || edge.source.port != domain::Port::output
            || edge.destination.port != domain::Port::input)
            return {{}, invalid("Structural connection identity/direction is invalid.")};
        result.connections.push_back({edge.source.node, edge.destination.node, edge.adapter});
    }
    return {std::move(result), {}};
}

std::vector<std::uint64_t> AudioGraph::outputDependencies() const
{
    std::set<std::uint64_t> visited;
    std::vector<std::uint64_t> pending;
    for (const auto& node : nodes)
        if (node.type == AudioNodeType::output) pending.push_back(node.id);
    while (!pending.empty())
    {
        const auto id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second) continue;
        for (const auto& edge : connections)
            if (edge.destination == id) pending.push_back(edge.source);
    }
    return {visited.begin(), visited.end()};
}

AudioQuery<std::unique_ptr<PreparedAudioGraph>> PreparedAudioGraph::prepare(
    const AudioGraph& graph, AudioStreamFormat format, std::uint64_t revision) try
{
    if (!std::isfinite(format.sampleRate) || format.sampleRate < 8000 || format.sampleRate > 384000
        || format.maxFrames < 1 || format.maxFrames > 4096 || format.bufferFrames < 1
        || format.bufferFrames > format.maxFrames || format.inputChannels < 0 || format.inputChannels > 32
        || format.outputChannels < 1 || format.outputChannels > 32
        || graph.channels < 1 || graph.channels > 32 || revision == 0)
        return {{}, invalid("Require 8-384 kHz, 1-4096 frame capacity, 1-32 host channels, valid 1-32 processor layouts and a positive revision.")};
    if (graph.nodes.size() > 256 || graph.connections.size() > 256)
        return {{}, invalid("Basic runtime limit is 256 nodes/connections.")};

    std::map<std::uint64_t, const AudioNodeDefinition*> nodes;
    std::map<std::uint64_t, std::vector<std::uint64_t>> incoming;
    std::map<std::uint64_t, std::vector<std::uint64_t>> outgoing;
    std::size_t outputCount{};
    std::set<int> usedOutputSlots;
    int capturedChannels{};
    int capturedSources{};
    for (const auto& node : graph.nodes)
    {
        if (node.id == 0 || !nodes.emplace(node.id, &node).second || !std::isfinite(node.gain)
            || node.gain < 0 || node.gain > 4)
            return {{}, invalid("Duplicate/zero node ID or invalid gain (finite 0-4 required).")};
        if (node.type < AudioNodeType::input || node.type > AudioNodeType::sum)
            return {{}, invalid("Unsupported processor type.")};
        if (node.channels < 0 || node.channels > 32)
            return {{}, invalid("Processor bus widths must be within 1-32 when explicitly declared.")};
        if (node.hardwareEndpoint && (node.type == AudioNodeType::input || node.type == AudioNodeType::output)
            && (node.channels < 1 || node.channels > 32))
            return {{}, invalid("Hardware endpoint bus width must be within 1-32 channels.")};
        if (node.type == AudioNodeType::output)
        {
            ++outputCount;
            if (node.hardwareEndpoint && node.endpointActive
                && node.hardwareChannelMap.size() != static_cast<std::size_t>(node.channels > 0 ? node.channels : graph.channels))
                return {{}, invalid("An enabled hardware destination requires one resolved device channel per bus channel.")};
            if (!node.hardwareChannelMap.empty())
                for (const auto slot : node.hardwareChannelMap)
                    if (slot < 0 || slot >= format.outputChannels || !usedOutputSlots.insert(slot).second)
                        return {{}, invalid("Output rows must map to disjoint compact stream channels; resolve overlaps before Start.")};
            else if (!node.hardwareEndpoint)
                for (int slot = 0; slot < (node.channels > 0 ? node.channels : graph.channels); ++slot)
                    if (slot >= format.outputChannels || !usedOutputSlots.insert(slot).second)
                        return {{}, invalid("Identity-mapped Output must occupy a unique host channel group; use explicit endpoint mappings for multiple outputs.")};
        }
        if (node.type == AudioNodeType::input && node.hardwareEndpoint && node.endpointActive
            && node.hardwareChannelMap.size() != static_cast<std::size_t>(node.channels > 0 ? node.channels : graph.channels))
            return {{}, invalid("An enabled hardware source requires one resolved device channel per bus channel.")};
        const auto hostChannels = node.type == AudioNodeType::input ? format.inputChannels : format.outputChannels;
        if ((node.type == AudioNodeType::input || node.type == AudioNodeType::output)
            && !node.hardwareChannelMap.empty())
        {
            std::set<int> unique;
            for (const auto slot : node.hardwareChannelMap)
                if (slot < 0 || slot >= hostChannels || !unique.insert(slot).second)
                    return {{}, invalid("Endpoint channel maps must use unique in-range compact host-buffer slots.")};
        }
        if (!node.hardwareEndpoint && node.hardwareChannelMap.empty()
            && node.type == AudioNodeType::output
            && (node.channels > 0 ? node.channels : graph.channels) != format.outputChannels)
            return {{}, invalid("Legacy identity-mapped Output must match the host output width; use a resolved endpoint map or explicit adapter.")};
        if (!node.hardwareEndpoint && node.hardwareChannelMap.empty()
            && node.type == AudioNodeType::input && format.inputChannels != 0
            && (node.channels > 0 ? node.channels : graph.channels) != format.inputChannels)
            return {{}, invalid("Legacy identity-mapped Input must match the host input width; use a resolved endpoint map or explicit adapter.")};
        if (node.capture)
        {
            capturedChannels += node.capture->channels();
            ++capturedSources;
            if (node.capture->source() != node.id || node.capture->sampleRate() != format.sampleRate
                || node.capture->firstChannel() + node.capture->channels() > 32 || capturedChannels > 16
                || capturedSources > 8 || capturedChannels * format.sampleRate > 768000)
                return {{}, invalid("Capture source/rate/channel mismatch or 16-channel observation limit exceeded.")};
        }
    }
    if (!nodes.empty() && outputCount == 0)
        return {{}, invalid("A nonempty runtime graph requires at least one hardware destination row.")};
    std::set<std::pair<std::uint64_t, std::uint64_t>> edges;
    for (const auto& edge : graph.connections)
    {
        if (!nodes.contains(edge.source) || !nodes.contains(edge.destination) || edge.source == edge.destination
            || nodes.at(edge.source)->type == AudioNodeType::output || nodes.at(edge.destination)->type == AudioNodeType::input
            || isGenerator(nodes.at(edge.destination)->type)
            || !edges.emplace(edge.source, edge.destination).second
            || (incoming.contains(edge.destination) && nodes.at(edge.destination)->type != AudioNodeType::sum)
            || (edge.adapter != domain::AudioAdapter::none && edge.adapter != domain::AudioAdapter::monoToStereo))
            return {{}, invalid("Invalid/missing endpoint, direction, self-link, adapter or multiple input sources.")};
        const auto& destination = *nodes.at(edge.destination);
        incoming[edge.destination].push_back(edge.source);
        outgoing[edge.source].push_back(edge.destination);
    }
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<>> ready;
    std::map<std::uint64_t, std::size_t> degrees;
    for (const auto& [id, node] : nodes)
    {
        degrees[id] = incoming[id].size();
        if (degrees[id] == 0) ready.push(id);
    }
    std::vector<std::uint64_t> order;
    while (!ready.empty())
    {
        const auto id = ready.top(); ready.pop();
        order.push_back(id);
        for (const auto next : outgoing[id])
            if (--degrees.at(next) == 0) ready.push(next);
    }
    if (order.size() != nodes.size()) return {{}, invalid("Runtime graph contains a directed cycle.")};

    std::map<std::uint64_t, int> widths;
    for (const auto id : order)
    {
        const auto& node = *nodes.at(id);
        const auto& upstream = incoming.at(id);
        const auto convertedWidth = [&](const AudioConnection& edge)
        {
            const auto sourceWidth = widths.at(edge.source);
            return edge.adapter == domain::AudioAdapter::monoToStereo ? 2 : sourceWidth;
        };
        if (node.type == AudioNodeType::input || node.type == AudioNodeType::output)
            widths[id] = node.channels > 0 ? node.channels : graph.channels;
        else if (isGenerator(node.type))
            widths[id] = node.channels > 0 ? node.channels : graph.channels;
        else if (node.type == AudioNodeType::sum)
        {
            if (upstream.empty())
            {
                widths[id] = node.channels > 0 ? node.channels : graph.channels;
                continue;
            }
            const auto first = std::find_if(graph.connections.begin(), graph.connections.end(),
                [id](const auto& edge) { return edge.destination == id; });
            const auto width = convertedWidth(*first);
            if (width < 1 || width > 32)
                return {{}, invalid("Sum bus width must be within 1-32 channels.")};
            for (const auto& edge : graph.connections)
                if (edge.destination == id && convertedWidth(edge) != width)
                    return {{}, invalid("Sum inputs need matching channel layouts; add an explicit adapter before summing.")};
            widths[id] = width;
        }
        else
        {
            if (upstream.size() > 1)
                return {{}, invalid("A processor input accepts one feed; use the explicit Sum utility to mix.")};
            widths[id] = upstream.empty() ? graph.channels : 1;
            if (!upstream.empty())
            {
                const auto edge = std::find_if(graph.connections.begin(), graph.connections.end(),
                    [id](const auto& item) { return item.destination == id; });
                widths[id] = convertedWidth(*edge);
            }
        }
        if (widths[id] < 1 || widths[id] > 32)
            return {{}, invalid("Every prepared processor bus must have 1-32 channels.")};
    }

    for (const auto& edge : graph.connections)
    {
        const auto sourceWidth = widths.at(edge.source);
        const auto destinationType = nodes.at(edge.destination)->type;
        const auto destinationWidth = destinationType == AudioNodeType::sum
            ? widths.at(edge.destination)
            : widths.at(edge.destination);
        if (edge.adapter == domain::AudioAdapter::monoToStereo)
        {
            if (sourceWidth != 1 || destinationWidth != 2)
                return {{}, invalid("Dual-mono adapter is defined only for one mono source channel to a two-channel destination.")};
        }
        else if (sourceWidth != destinationWidth)
            return {{}, invalid("Connected ports have incompatible channel dimensions; select an explicit supported adapter.")};
    }

    const auto dependencies = graph.outputDependencies();
    const std::set<std::uint64_t> reachable(dependencies.begin(), dependencies.end());

    auto plan = std::unique_ptr<PreparedAudioGraph>(new PreparedAudioGraph);
    plan->format_ = format;
    plan->revision_ = revision;
    std::size_t scratchFloats{};
    std::map<std::uint64_t, std::size_t> offsets;
    for (auto id : order)
    {
        const auto channelCount = static_cast<std::size_t>(widths.at(id));
        if (scratchFloats > std::numeric_limits<std::size_t>::max() - channelCount * static_cast<std::size_t>(format.maxFrames))
            return {{}, invalid("Prepared bus scratch size overflow.")};
        offsets.emplace(id, scratchFloats);
        scratchFloats += channelCount * static_cast<std::size_t>(format.maxFrames);
    }
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> adapterOffsets;
    for (const auto& edge : graph.connections)
        if (edge.adapter != domain::AudioAdapter::none)
        {
            const auto channelCount = static_cast<std::size_t>(widths.at(edge.destination));
            adapterOffsets.emplace(std::pair{edge.source, edge.destination}, scratchFloats);
            scratchFloats += channelCount * static_cast<std::size_t>(format.maxFrames);
        }
    if (scratchFloats > 64 * 1024 * 1024 / sizeof(float))
        return {{}, invalid("Prepared scratch exceeds the 64 MiB runtime budget.")};
    plan->scratch_.resize(scratchFloats, 0.0f);
    std::size_t storageBytes = plan->scratch_.size() * sizeof(float);
    for (auto id : order)
    {
        const auto& node = *nodes.at(id);
        const auto destination = offsets.at(id);
        const auto channelCount = widths.at(id);
        auto processor = createProcessor(node.type, node.gain);
        if (!node.userScale.empty())
        {
            auto* signal = dynamic_cast<SignalGeneratorNode*>(processor.get());
            if (!signal || !signal->setUserScale(node.userScale))
                return {{}, invalid("Signal Generator custom scale configuration is invalid.")};
        }
        const ProcessingSpec spec{format.sampleRate, format.maxFrames, channelCount};
        storageBytes += processor->requiredStorageBytes(spec);
        if (storageBytes > 128 * 1024 * 1024)
            return {{}, invalid("Prepared DSP storage exceeds the 128 MiB graph budget.")};
        std::set<std::uint16_t> assigned;
        if (node.type == AudioNodeType::sum && (!node.parameters.empty() || !node.controls.empty() || node.bypassed))
            return {{}, invalid("Sum is a unity-coefficient routing utility with no gain, control or bypass parameters.")};
        for (const auto& [parameter, value] : node.parameters)
            if (!assigned.insert(parameter).second || !processor->initialParameter(parameter, value))
                return {{}, invalid("Duplicate, unknown or out-of-range initial processor parameter.")};
        for (const auto& control : node.controls)
            if (!processor->initialControl(control))
                return {{}, invalid("Invalid/duplicate/out-of-domain initial control mapping.")};
        if (node.bypassed && (node.type == AudioNodeType::input || node.type == AudioNodeType::output))
            return {{}, invalid("Device routing nodes do not support bypass.")};
        processor->initialBypass(node.bypassed);
        if (const auto prepared = processor->prepare(spec); !prepared.succeeded)
            return {{}, invalid(prepared.error)};
        if (node.type == AudioNodeType::analyzer)
            static_cast<AnalyzerNode*>(processor.get())->bindCapture(node.capture);
        std::vector<InputEdge> inputEdges;
        for (const auto& edge : graph.connections)
            if (edge.destination == id)
            {
                const auto sourceOffset = offsets.at(edge.source);
                const auto adapter = edge.adapter;
                const auto scratch = adapter == domain::AudioAdapter::none ? sourceOffset
                    : adapterOffsets.at({edge.source, edge.destination});
                inputEdges.push_back({sourceOffset, scratch, widths.at(edge.source), adapter});
            }
        plan->steps_.push_back({id, node.type, std::move(processor), destination, channelCount, node.capture,
                                node.hardwareChannelMap, std::move(inputEdges), node.endpointActive,
                                reachable.contains(id)});
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
    {
        if (!step.active) continue;
        for (const auto& edge : step.inputs)
            if (edge.adapter == domain::AudioAdapter::monoToStereo)
            {
                const auto* source = scratch_.data() + edge.source + offset;
                auto* destination = scratch_.data() + edge.scratch + offset;
                const auto destinationStride = format_.maxFrames;
                std::copy_n(source, frames, destination);
                std::copy_n(source, frames, destination + destinationStride);
            }
        const auto inputSource = step.inputs.empty() ? step.destination : step.inputs.front().scratch;
        if (step.type == AudioNodeType::sum)
        {
            for (int channel = 0; channel < step.channels; ++channel)
            {
                auto* destination = scratch_.data() + step.destination + channel * format_.maxFrames + offset;
                std::fill_n(destination, frames, 0.0f);
                for (const auto& edge : step.inputs)
                    for (int frame = 0; frame < frames; ++frame)
                        destination[frame] += scratch_[edge.scratch + channel * format_.maxFrames + offset + frame];
            }
            step.processor->process({scratch_.data() + step.destination + offset, scratch_.data() + step.destination + offset,
                                      step.channels, format_.maxFrames, frames, buffer, offset});
        }
        else
        {
            if (step.inputs.empty() && step.type != AudioNodeType::input && step.type != AudioNodeType::output
                && !isGenerator(step.type))
                for (int channel = 0; channel < step.channels; ++channel)
                    std::fill_n(scratch_.data() + step.destination + channel * format_.maxFrames + offset, frames, 0.0f);
            const auto* map = step.hardwareChannelMap.empty() ? nullptr : step.hardwareChannelMap.data();
            step.processor->process({scratch_.data() + inputSource + offset, scratch_.data() + step.destination + offset,
                                      step.channels, format_.maxFrames, frames, buffer, offset, map,
                                      step.endpointActive});
        }
    }
}
void PreparedAudioGraph::capture(int frames, std::uint64_t epoch, std::uint64_t firstFrame) noexcept
{
    for (auto& step : steps_)
        if (step.capture && step.active)
        {
            const auto processed = step.processor->processedFrames();
            const auto start = processed >= static_cast<std::uint64_t>(frames)
                ? processed - static_cast<std::uint64_t>(frames) : 0;
            step.capture->capture(scratch_.data() + step.destination, format_.maxFrames, frames,
                                  revision_, epoch, firstFrame, start);
        }
}
bool PreparedAudioGraph::parameterIndex(std::uint64_t node, std::uint16_t parameter, float value,
                                        ParameterAction action, std::size_t& index) const noexcept
{
    for (std::size_t i = 0; i < steps_.size(); ++i)
        if (steps_[i].id == node && steps_[i].type != AudioNodeType::input && steps_[i].type != AudioNodeType::output
            && steps_[i].type != AudioNodeType::sum
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
