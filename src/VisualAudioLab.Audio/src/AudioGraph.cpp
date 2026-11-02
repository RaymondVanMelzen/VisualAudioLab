#include <val/audio/AudioGraph.h>
#include <val/audio/GeneratorNodes.h>
#include <val/audio/ScriptNode.h>
#include <val/script/Compiler.h>
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
std::unique_ptr<AudioNode> createProcessor(AudioNodeType type, float gain = 1, std::shared_ptr<const script::Program> script = {})
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
        case AudioNodeType::script: return script ? std::make_unique<ScriptNode>(std::move(script)) : nullptr;
        case AudioNodeType::signalGenerator: return std::make_unique<SignalGeneratorNode>();
        case AudioNodeType::impulseGenerator: return std::make_unique<ImpulseGeneratorNode>();
        case AudioNodeType::clickGenerator: return std::make_unique<ClickGeneratorNode>();
        case AudioNodeType::burstGenerator: return std::make_unique<BurstGeneratorNode>();
        case AudioNodeType::chirpGenerator: return std::make_unique<ChirpGeneratorNode>();
        case AudioNodeType::noiseGenerator: return std::make_unique<NoiseGeneratorNode>();
        case AudioNodeType::dcGenerator: return std::make_unique<DcGeneratorNode>();
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
        else if (*type == "Script")
        {
            definition.type = AudioNodeType::script;
            const auto fieldSource = node.parameters.find("audio.script");
            const auto* source = fieldSource != node.parameters.end() ? std::get_if<std::string>(&fieldSource->second) : nullptr;
            if (!source) return {{}, invalid("Script node " + std::to_string(id) + " requires audio.script source text.")};
            const auto compiled = script::Compiler::compile(*source);
            if (!compiled.value)
            {
                const auto& diagnostic = compiled.diagnostics.front();
                return {{}, invalid("Script node " + std::to_string(id) + " at " + std::to_string(diagnostic.source.line)
                    + ":" + std::to_string(diagnostic.source.column) + ": " + diagnostic.message)};
            }
            definition.script = *compiled.value;
        }
        else if (*type == "Signal Generator") definition.type = AudioNodeType::signalGenerator;
        else if (*type == "Impulse Generator") definition.type = AudioNodeType::impulseGenerator;
        else if (*type == "Click Generator") definition.type = AudioNodeType::clickGenerator;
        else if (*type == "Burst Generator") definition.type = AudioNodeType::burstGenerator;
        else if (*type == "Chirp Generator") definition.type = AudioNodeType::chirpGenerator;
        else if (*type == "Noise Generator") definition.type = AudioNodeType::noiseGenerator;
        else if (*type == "DC Generator") definition.type = AudioNodeType::dcGenerator;
        else return {{}, invalid("Unsupported audio.type on node " + std::to_string(id) + ".")};
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
                if (definition.type == AudioNodeType::script)
                    for (auto& c : name) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                const auto* value = std::get_if<double>(&scalar);
                if (!value || !std::isfinite(*value)) return {{}, invalid("audio.param values must be finite numbers.")};
                // Parameter identities are names in projects and immutable indices in prepared plans.
                const auto prototype = createProcessor(definition.type, 1, definition.script);
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
            auto prototype = createProcessor(definition.type, 1, definition.script);
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
                else if (*source == "script")
                {
                    binding.source = ParameterControlSource::script;
                    const auto* sourceText = getString("script");
                    if (!sourceText || !getNumber("minimum", binding.sourceMinimum)
                        || !getNumber("maximum", binding.sourceMaximum))
                        return {{}, invalid("Script control requires source text and declared finite minimum/maximum.")};
                    const auto compiled = script::Compiler::compile(*sourceText);
                    if (!compiled.value)
                    {
                        const auto& diagnostic = compiled.diagnostics.front();
                        return {{}, invalid("Control Script at " + std::to_string(diagnostic.source.line)
                            + ":" + std::to_string(diagnostic.source.column) + ": " + diagnostic.message)};
                    }
                    binding.program = *compiled.value;
                    if (!binding.program->inputs().size() || binding.program->outputs().size() != 1
                        || !binding.program->parameters().empty()
                        || binding.program->inputs().size() != 1)
                        return {{}, invalid("Control Script must declare exactly INPUT Time: REAL, one REAL OUTPUT, and no parameters.")};
                }
                else return {{}, invalid("Control source must be lfo, envelope or script.")};
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
    int capturedChannels{};
    int capturedSources{};
    std::size_t scriptInstructions{};
    for (const auto& node : graph.nodes)
    {
        if (node.id == 0 || !nodes.emplace(node.id, &node).second || !std::isfinite(node.gain)
            || node.gain < 0 || node.gain > 4)
            return {{}, invalid("Duplicate/zero node ID or invalid gain (finite 0-4 required).")};
        if (node.type < AudioNodeType::input || node.type > AudioNodeType::dcGenerator)
            return {{}, invalid("Unsupported processor type.")};
        if (node.type == AudioNodeType::script)
        {
            if (!node.script) return {{}, invalid("Script node requires a verified compiler artifact, not deferred source compilation.")};
            scriptInstructions += node.script->instructions().size();
            if (scriptInstructions > 4096) return {{}, invalid("Graph script budget exceeds 4096 expanded instructions per frame.")};
        }
        else if (node.script) return {{}, invalid("A script artifact may only be attached to a Script processor.")};
        for (const auto& control : node.controls)
            if (control.program)
            {
                scriptInstructions += control.program->instructions().size();
                if (scriptInstructions > 4096)
                    return {{}, invalid("Graph Script/control budget exceeds 4096 expanded instructions per frame.")};
            }
        if (node.capture)
        {
            capturedChannels += node.capture->channels();
            ++capturedSources;
            if (node.capture->source() != node.id || node.capture->sampleRate() != format.sampleRate
                || node.capture->firstChannel() + node.capture->channels() > graph.channels || capturedChannels > 16
                || capturedSources > 8 || capturedChannels * format.sampleRate > 768000)
                return {{}, invalid("Capture source/rate/channel mismatch or 16-channel observation limit exceeded.")};
        }
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
            || isGenerator(nodes.at(edge.destination)->type)
            || !incoming.emplace(edge.destination, edge.source).second)
            return {{}, invalid("Invalid/missing endpoint, direction, self-link or multiple input sources.")};
        const auto& destination = *nodes.at(edge.destination);
        if (destination.type == AudioNodeType::script && destination.script->inputs().empty())
            return {{}, invalid("A zero-input Script generator cannot accept an upstream connection.")};
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
        auto processor = createProcessor(node.type, node.gain, node.script);
        if (!node.userScale.empty())
        {
            auto* signal = dynamic_cast<SignalGeneratorNode*>(processor.get());
            if (!signal || !signal->setUserScale(node.userScale))
                return {{}, invalid("Signal Generator custom scale configuration is invalid.")};
        }
        const ProcessingSpec spec{format.sampleRate, format.maxFrames, graph.channels};
        storageBytes += processor->requiredStorageBytes(spec);
        if (storageBytes > 128 * 1024 * 1024)
            return {{}, invalid("Prepared DSP storage exceeds the 128 MiB graph budget.")};
        std::set<std::uint16_t> assigned;
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
        plan->steps_.push_back({id, node.type, std::move(processor), source, destination, node.capture});
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
void PreparedAudioGraph::capture(int frames, std::uint64_t epoch, std::uint64_t firstFrame) noexcept
{
    for (auto& step : steps_)
        if (step.capture)
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
std::optional<script::RuntimeFault> PreparedAudioGraph::scriptFault(std::uint64_t node) const noexcept
{
    for (const auto& step : steps_)
        if (step.id == node && step.type == AudioNodeType::script)
            return static_cast<const ScriptNode*>(step.processor.get())->fault();
    return std::nullopt;
}
std::uint64_t PreparedAudioGraph::numericalFaults() const noexcept
{
    std::uint64_t faults{};
    for (const auto& step : steps_) faults += step.processor->numericalFaults();
    return faults;
}
}
