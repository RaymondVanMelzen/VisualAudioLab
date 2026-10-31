#include <val/presentation/RuntimePanel.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace val::presentation
{
using namespace application;
namespace
{
juce::String text(const std::string& value) { return juce::String::fromUTF8(value.c_str()); }
AudioError invalid(std::string value) { return {AudioErrorCode::invalidArgument, std::move(value)}; }
bool contentEqual(const domain::Graph& left, const domain::Graph& right)
{
    if (left.connections != right.connections || left.nodes.size() != right.nodes.size()) return false;
    for (const auto& [id, node] : left.nodes)
    {
        const auto found = right.nodes.find(id);
        if (found == right.nodes.end() || node.parameters != found->second.parameters) return false;
    }
    return true;
}
}
RuntimePanel::RuntimePanel(WorkspaceService& workspace, VisualizationSession& visualizations, BackendFactory factory)
    : workspace_(workspace), visualizations_(visualizations), factory_(std::move(factory))
{
    api_.addItem("ASIO (Windows)", 1); api_.addItem("WASAPI Exclusive (Windows)", 2);
    api_.addItem("WASAPI Shared (Windows)", 3); api_.addItem("CoreAudio (macOS)", 4);
    api_.addItem("Offline preview (no hardware)", 5);
#if JUCE_MAC
    api_.setSelectedId(4, juce::dontSendNotification);
#else
    api_.setSelectedId(1, juce::dontSendNotification);
#endif
    for (auto* control : std::initializer_list<juce::Component*>{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_,
             &scan_, &inspect_, &start_, &stop_, &notice_, &state_}) addAndMakeVisible(control);
    const juce::String names[]{"Backend", "Input device", "Output device", "Sample rate", "Buffer frames", "Input channels", "Output channels"};
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_};
    for (int i = 0; i < 7; ++i)
    {
        labels_[i].setText(names[i], juce::dontSendNotification); addAndMakeVisible(labels_[i]);
        boxes[i]->setTitle(names[i]); boxes[i]->setTextWhenNothingSelected("Select explicitly");
    }
    notice_.setText("Check output levels before Start; there is no master limiter. Inspect may initialize drivers (ASIO may briefly start/stop). "
                    "Capability lists are candidates, not guaranteed combinations. No automatic fallback.", juce::dontSendNotification);
    notice_.setFont(juce::FontOptions(11.0f));
    state_.setText(lastStatus_, juce::dontSendNotification); state_.setFont(juce::FontOptions(11.0f));
    api_.onChange = [this] { selectBackend(); };
    scan_.onClick = [this] { scan(); }; inspect_.onClick = [this] { inspect(); };
    input_.onChange = output_.onChange = [this]
    {
        ++probeSerial_; probing_ = false; inputCaps_.reset(); outputCaps_.reset();
        rateChoice_.clear(); buffer_.clear(); inputCount_.clear(); outputCount_.clear();
        message("Selection changed. Inspect selected devices before starting."); enableControls();
    };
    start_.onClick = [this] { const auto result = start(); if (!result.succeeded) message(text(result.error)); };
    stop_.onClick = [this] { const auto result = stop(); if (!result.succeeded) message(text(result.error)); };
    startTimerHz(30);
    enableControls();
}
RuntimePanel::~RuntimePanel()
{
    stopTimer(); onStatus = nullptr; onStarted = nullptr;
    const auto stopped = stop(); juce::ignoreUnused(stopped);
    if (backend_) { const auto result = backend_->shutdown(); juce::ignoreUnused(result); }
}
void RuntimePanel::message(juce::String value)
{
    lastStatus_ = std::move(value); state_.setText(lastStatus_, juce::dontSendNotification);
    if (onStatus) onStatus(lastStatus_);
}
void RuntimePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    auto buttons = bounds.removeFromTop(30);
    start_.setBounds(buttons.removeFromLeft(70).reduced(2)); stop_.setBounds(buttons.removeFromLeft(70).reduced(2));
    scan_.setBounds(buttons.removeFromLeft(110).reduced(2)); inspect_.setBounds(buttons.reduced(2));
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_};
    // Two compact columns keep the non-modal settings useful in the default bottom dock.
    for (int i = 0; i < 7; ++i)
    {
        const int row = i / 2, col = i % 2, column = bounds.getWidth() / 2;
        auto cell = juce::Rectangle<int>(bounds.getX() + col * column, bounds.getY() + row * 28, column, 28).reduced(2);
        labels_[i].setBounds(cell.removeFromLeft(100)); boxes[i]->setBounds(cell);
    }
    bounds.removeFromTop(112);
    notice_.setBounds(bounds.removeFromTop(38)); state_.setBounds(bounds);
}
void RuntimePanel::enableControls()
{
    for (auto* component : std::initializer_list<juce::Component*>{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_})
        component->setEnabled(!running_ && !probing_);
    scan_.setEnabled(!running_ && !probing_ && !offline_ && static_cast<bool>(factory_));
    inspect_.setEnabled(!running_ && !probing_ && !offline_ && backend_ != nullptr);
    start_.setEnabled(!running_ && !probing_);
    stop_.setEnabled(running_);
}
void RuntimePanel::selectBackend()
{
    if (running_) return;
    ++probeSerial_; probing_ = false;
    if (backend_) { const auto result = backend_->shutdown(); if (!result.succeeded()) message(text(result.message)); backend_.reset(); }
    inputs_.clear(); outputs_.clear(); input_.clear(); output_.clear();
    inputCaps_.reset(); outputCaps_.reset(); rateChoice_.clear(); buffer_.clear(); inputCount_.clear(); outputCount_.clear();
    offline_ = api_.getSelectedId() == 5;
    if (offline_) configureOffline(2);
    else message("Stopped. Click Scan devices; discovery is not automatic.");
    enableControls();
}
void RuntimePanel::scan()
{
    if (running_ || !factory_) { message("Native backend composition unavailable, or stop transport before scanning."); return; }
    if (!backend_)
    {
        backend_ = factory_(static_cast<AudioBackendKind>(api_.getSelectedId() - 1));
        if (!backend_) { message("Selected backend is unavailable in this host."); return; }
        const auto listen = backend_->setChangeCallback([safe = juce::Component::SafePointer<RuntimePanel>(this)](const auto& snapshot)
        { if (safe) safe->inventory(snapshot); });
        if (!listen.succeeded()) { message(text(listen.message)); return; }
    }
    const auto result = backend_->refresh();
    if (!result.succeeded()) message(text(result.message));
    else message("Scanning selected backend...");
    enableControls();
}
void RuntimePanel::inventory(const AudioBackendSnapshot& snapshot)
{
    ++probeSerial_; probing_ = false;
    if (running_)
    {
        const auto result = stop();
        if (!result.succeeded) { message(text(result.error)); return; }
        message("Device inventory changed. Transport stopped; reselect and inspect before restarting.");
    }
    inputCaps_.reset(); outputCaps_.reset(); inputs_.clear(); outputs_.clear();
    input_.clear(); output_.clear(); rateChoice_.clear(); buffer_.clear(); inputCount_.clear(); outputCount_.clear();
    input_.addItem("None (output only)", 1); input_.setSelectedId(1, juce::dontSendNotification);
    for (const auto& device : snapshot.devices)
    {
        if (device.direction != AudioDirection::output) { inputs_.push_back(device); input_.addItem(text(device.name), static_cast<int>(inputs_.size()) + 1); }
        if (device.direction != AudioDirection::input) { outputs_.push_back(device); output_.addItem(text(device.name), static_cast<int>(outputs_.size())); }
    }
    if (!snapshot.error.succeeded()) message(text(snapshot.error.message));
    else message("Inventory ready. Select endpoints and click Inspect selected devices. " + text(snapshot.notes.empty() ? "" : snapshot.notes.front()));
    enableControls();
}
void RuntimePanel::inspect()
{
    if (!backend_ || running_ || output_.getSelectedId() < 1) { message("Select an output device first."); return; }
    inputCaps_.reset(); outputCaps_.reset(); probing_ = true;
    const auto serial = ++probeSerial_;
    auto safe = juce::Component::SafePointer<RuntimePanel>(this);
    const auto result = backend_->queryCapabilities(outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token,
        [safe, serial](auto query)
        {
            if (!safe || safe->probeSerial_ != serial) return;
            safe->capabilities(false, std::move(query));
        });
    if (!result.succeeded()) { probing_ = false; message(text(result.message)); }
    else if (probing_) message("Inspecting selected output. Driver initialization may touch hardware.");
    enableControls();
}
void RuntimePanel::capabilities(bool input, AudioQuery<AudioDeviceCapabilities> result)
{
    if (!result.value) { probing_ = false; message(text(result.error.message)); enableControls(); return; }
    if (input) inputCaps_ = std::move(*result.value); else outputCaps_ = std::move(*result.value);
    if (!input && input_.getSelectedId() > 1)
    {
        const auto serial = probeSerial_; auto safe = juce::Component::SafePointer<RuntimePanel>(this);
        const auto requestResult = backend_->queryCapabilities(inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).token,
            [safe, serial](auto query) { if (safe && safe->probeSerial_ == serial) safe->capabilities(true, std::move(query)); });
        if (!requestResult.succeeded()) { probing_ = false; message(text(requestResult.message)); enableControls(); }
        return;
    }
    probing_ = false; populateConfiguration(); enableControls();
}
void RuntimePanel::populateConfiguration()
{
    rateChoice_.clear(); buffer_.clear(); inputCount_.clear(); outputCount_.clear();
    inputCount_.addItem("0", 1); // Native input count uses ID count+1, including explicit zero.
    if (inputCaps_)
        for (int i = 1; i <= std::min(32, static_cast<int>(inputCaps_->inputChannels.size())); ++i) inputCount_.addItem(juce::String(i), i + 1);
    if (outputCaps_)
    {
        juce::StringArray channelNames;
        for (const auto& channel : outputCaps_->outputChannels) channelNames.add(text(channel));
        outputCount_.setTooltip("Channels are the first N reported channels: " + channelNames.joinIntoString(", "));
        channelNames.clear();
        if (inputCaps_) for (const auto& channel : inputCaps_->inputChannels) channelNames.add(text(channel));
        inputCount_.setTooltip("Channels are the first N reported channels: " + channelNames.joinIntoString(", "));
        for (int i = 1; i <= std::min(32, static_cast<int>(outputCaps_->outputChannels.size())); ++i) outputCount_.addItem(juce::String(i), i);
        int id = 1;
        for (const auto rate : outputCaps_->sampleRates)
            if (!inputCaps_ || std::find(inputCaps_->sampleRates.begin(), inputCaps_->sampleRates.end(), rate) != inputCaps_->sampleRates.end())
                rateChoice_.addItem(juce::String(rate, 0), id++);
        id = 1;
        for (const auto frames : outputCaps_->bufferFrames)
            if (!inputCaps_ || std::find(inputCaps_->bufferFrames.begin(), inputCaps_->bufferFrames.end(), frames) != inputCaps_->bufferFrames.end())
                buffer_.addItem(juce::String(frames), id++);
    }
    inputCount_.setSelectedId(1, juce::dontSendNotification);
    message((rateChoice_.getNumItems() == 0 || buffer_.getNumItems() == 0
                ? "No common reported rate/buffer candidates. This configuration cannot start; select different endpoints/API. "
                : "Capabilities ready. Explicitly select rate, buffer and output channels. Input count must be zero or match output. ")
            + (outputCaps_ && !outputCaps_->notes.empty() ? text(outputCaps_->notes.front()) : juce::String{}));
}
void RuntimePanel::configureOffline(int channels, double rate, int frames)
{
    if (running_) return;
    offline_ = true; api_.setSelectedId(5, juce::dontSendNotification);
    rateChoice_.clear(); buffer_.clear(); inputCount_.clear(); outputCount_.clear();
    rateChoice_.addItem(juce::String(rate, 0), 1); rateChoice_.setSelectedId(1, juce::dontSendNotification);
    buffer_.addItem(juce::String(frames), 1); buffer_.setSelectedId(1, juce::dontSendNotification);
    inputCount_.addItem("0", 1); inputCount_.setSelectedId(1, juce::dontSendNotification);
    for (int i = 1; i <= 32; ++i) outputCount_.addItem(juce::String(i), i);
    outputCount_.setSelectedId(channels, juce::dontSendNotification);
    message("Offline preview selected. Start processes silent input / script generators to visualizers only; no device opens.");
    enableControls();
}
AudioQuery<AudioStreamRequest> RuntimePanel::request() const
{
    AudioStreamRequest value;
    value.sampleRate = rateChoice_.getText().getDoubleValue(); value.bufferFrames = buffer_.getText().getIntValue();
    value.maxFrames = std::max(512, value.bufferFrames); value.inputChannels = inputCount_.getSelectedId() - 1;
    value.outputChannels = outputCount_.getSelectedId();
    if (value.sampleRate < 8000 || value.sampleRate > 384000 || value.bufferFrames < 1 || value.bufferFrames > 4096
        || value.outputChannels < 1 || value.outputChannels > 32 || value.inputChannels < 0
        || (value.inputChannels != 0 && value.inputChannels != value.outputChannels))
        return {{}, invalid("Select a reported rate/buffer and matching input/output counts, or zero inputs. 8-384 kHz, 1-4096 frames, 1-32 outputs.")};
    if (!offline_)
    {
        if (!outputCaps_ || output_.getSelectedId() < 1 || !backend_) return {{}, invalid("Scan/select/inspect native devices first.")};
        value.output = outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token;
        if (value.inputChannels)
        {
            if (!inputCaps_ || input_.getSelectedId() < 2) return {{}, invalid("Inspect an input device or select zero inputs.")};
            value.input = inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).token;
        }
    }
    return {value, {}};
}
AudioQuery<audio::AudioGraph> RuntimePanel::compileGraph(bool captures)
{
    auto compiled = audio::AudioGraph::fromStructural(workspace_.graph(), width_);
    if (!compiled.value) return compiled;
    if (captures)
        for (auto& node : compiled.value->nodes)
        {
            const auto existing = captures_.find(node.id);
            if (existing != captures_.end()) node.capture = existing->second;
        }
    return compiled;
}
Result RuntimePanel::attachCaptures(audio::AudioGraph& graph, std::vector<std::uint64_t>& added)
{
    const int captureChannels = std::min(2, width_);
    const int limit = std::min(8, static_cast<int>(768000 / (rate_ * captureChannels)));
    std::vector<std::uint64_t> observed;
    for (const auto& node : graph.nodes) if (node.type == audio::AudioNodeType::analyzer) observed.push_back(node.id);
    for (const auto& node : graph.nodes)
        if (node.type != audio::AudioNodeType::input && node.type != audio::AudioNodeType::analyzer) observed.push_back(node.id);
    for (auto id : observed)
    {
        if (captures_.contains(id) || static_cast<int>(captures_.size()) >= limit) continue;
        auto capture = std::make_shared<audio::SignalCaptureBuffer>(id, rate_, captureChannels);
        const auto registered = visualizations_.add(capture, text(workspace_.graph().nodes.at(id).name));
        if (!registered.succeeded) return registered;
        captures_.emplace(id, std::move(capture)); added.push_back(id);
        visualizations_.setState(id, ObservationState::processing);
    }
    for (auto& node : graph.nodes)
        if (const auto found = captures_.find(node.id); found != captures_.end()) node.capture = found->second;
    return Result::success();
}
Result RuntimePanel::start()
{
    if (running_) return Result::failure("Transport is already running.");
    if (probing_) return Result::failure("Wait for device inspection to finish before starting transport.");
    const auto configuration = request();
    if (!configuration.value) return Result::failure(configuration.error.message);
    const auto& config = *configuration.value;
    width_ = config.outputChannels; rate_ = config.sampleRate; block_ = config.bufferFrames;
    auto compiled = compileGraph(false);
    if (!compiled.value) return Result::failure(compiled.error.message);
    if (compiled.value->nodes.empty()) return Result::failure("Patch is empty. Insert and connect modules (for example Script -> Output).");
    const AudioStreamFormat format{rate_, block_, config.maxFrames, config.inputChannels, width_};
    const auto validated = audio::PreparedAudioGraph::prepare(*compiled.value, format, 1);
    if (!validated.value) return Result::failure(validated.error.message);
    visualizations_.reset(); captures_.clear();
    std::vector<std::uint64_t> added;
    const auto attached = attachCaptures(*compiled.value, added);
    if (!attached.succeeded) { visualizations_.reset(); captures_.clear(); return attached; }
    const auto started = offline_ ? engine_.startOffline(format, *compiled.value, 1)
                                 : engine_.start(*backend_, config, *compiled.value, 1);
    if (!started.succeeded) { visualizations_.reset(); captures_.clear(); return started; }
    const auto actual = engine_.streamFormat();
    jassert(actual.value);
    rate_ = actual.value->sampleRate; block_ = actual.value->bufferFrames;
    running_ = true; revision_ = 1; patch_ = workspace_.project().activePatch;
    graphWarning_.clear();
    accepted_ = workspace_.graph(); acceptedAudio_ = *compiled.value;
    for (int i = 0; i < width_; ++i) offlinePointers_[i] = offlineOutput_.data() + i * block_;
    for (const auto& [id, capture] : captures_) { juce::ignoreUnused(capture); visualizations_.setState(id, ObservationState::processing); }
    message(offline_ ? "Offline preview running (no hardware, not realtime scheduling)." : "Audio running. Stop before changing device configuration.");
    if (onStarted) onStarted();
    enableControls(); return Result::success();
}
Result RuntimePanel::stop()
{
    const auto result = engine_.stop();
    if (!result.succeeded) return result;
    running_ = false; graphWarning_.clear();
    visualizations_.reset(); captures_.clear(); accepted_ = {}; acceptedAudio_ = {};
    message("Stopped. No stream is open."); enableControls();
    return Result::success();
}
bool RuntimePanel::parameterOnly(const domain::Graph& next) const
{
    if (next.connections != accepted_.connections || next.nodes.size() != accepted_.nodes.size()) return false;
    for (const auto& [id, node] : next.nodes)
    {
        const auto old = accepted_.nodes.find(id);
        if (old == accepted_.nodes.end()) return false;
        // Removing a saved target restores its processor default via a prepared revision.
        for (const auto& [key, value] : old->second.parameters)
        {
            juce::ignoreUnused(value);
            if (key.starts_with("audio.param.") && !node.parameters.contains(key)) return false;
        }
        auto before = old->second.parameters, after = node.parameters;
        std::erase_if(before, [](const auto& pair) { return pair.first.starts_with("audio.param.") || pair.first == "audio.bypass" || pair.first == "audio.gain"; });
        std::erase_if(after, [](const auto& pair) { return pair.first.starts_with("audio.param.") || pair.first == "audio.bypass" || pair.first == "audio.gain"; });
        if (before != after) return false;
    }
    return true;
}
Result RuntimePanel::applyParameters(const domain::Graph& next)
{
    const auto mapped = audio::AudioGraph::fromStructural(next, width_);
    if (!mapped.value) return Result::failure(mapped.error.message);
    // Validate all changes before delivering any. Queue overflow is visible, never silently dropped.
    const auto format = engine_.streamFormat();
    if (!format.value) return Result::failure(format.error.message);
    const auto plan = audio::PreparedAudioGraph::prepare(*mapped.value, *format.value, revision_);
    if (!plan.value) return Result::failure(plan.error.message);
    for (const auto& node : mapped.value->nodes)
    {
        const auto previous = accepted_.nodes.at(node.id).parameters;
        if (previous == next.nodes.at(node.id).parameters) continue;
        const auto old = std::find_if(acceptedAudio_.nodes.begin(), acceptedAudio_.nodes.end(), [&](const auto& n) { return n.id == node.id; });
        if (node.type == audio::AudioNodeType::gain)
        {
            const auto target = [](const auto& definition)
            {
                const auto gain = std::find_if(definition.parameters.begin(), definition.parameters.end(), [](auto pair) { return pair.first == 0; });
                return gain == definition.parameters.end() ? definition.gain : gain->second;
            };
            if (target(*old) != target(node))
            {
                const auto applied = engine_.setParameter(revision_, node.id, 0, target(node), audio::ParameterAction::base);
                if (!applied.succeeded) return applied;
            }
        }
        for (const auto& [index, value] : node.parameters)
        {
            if (node.type == audio::AudioNodeType::gain && index == 0) continue;
            const auto was = std::find_if(old->parameters.begin(), old->parameters.end(), [&](auto pair) { return pair.first == index; });
            if (was != old->parameters.end() && was->second == value) continue;
            const auto applied = engine_.setParameter(revision_, node.id, index, value, audio::ParameterAction::base);
            if (!applied.succeeded) return applied;
        }
        if (old->bypassed != node.bypassed)
        {
            const auto applied = engine_.setParameter(revision_, node.id, audio::AudioNode::bypassParameter, node.bypassed ? 1.0f : 0.0f, audio::ParameterAction::base);
            if (!applied.succeeded) return applied;
        }
    }
    acceptedAudio_ = *mapped.value; accepted_ = next;
    return Result::success();
}
void RuntimePanel::synchronize()
{
    if (!running_) return;
    if (workspace_.project().activePatch != patch_) { const auto result = stop(); if (!result.succeeded) message(text(result.error)); return; }
    if (contentEqual(accepted_, workspace_.graph())) { graphWarning_.clear(); return; }
    if (parameterOnly(workspace_.graph()))
    {
        const auto result = applyParameters(workspace_.graph());
        if (!result.succeeded)
        {
            const auto stopped = stop();
            message("Parameter update failed; transport stopped to avoid partial target delivery. " + text(result.error)
                + (stopped.succeeded ? juce::String{} : " Stop failure: " + text(stopped.error)));
        }
        else message("Parameter targets updated (existing smoothing/layers preserved).");
        return;
    }
    auto compiled = compileGraph(true);
    if (!compiled.value) { graphWarning_ = "Patch not executable; last accepted audio continues. " + text(compiled.error.message); message(graphWarning_); return; }
    const auto actual = engine_.streamFormat();
    const auto valid = audio::PreparedAudioGraph::prepare(*compiled.value, *actual.value, revision_ + 1);
    if (!valid.value) { graphWarning_ = "Patch not executable; last accepted audio continues. " + text(valid.error.message); message(graphWarning_); return; }
    std::vector<std::uint64_t> added;
    const auto attached = attachCaptures(*compiled.value, added);
    if (!attached.succeeded)
    {
        for (auto id : added) { visualizations_.remove(id); captures_.erase(id); }
        graphWarning_ = text(attached.error); message(graphWarning_); return;
    }
    const auto result = engine_.publish(*compiled.value, revision_ + 1);
    if (!result.succeeded)
    {
        for (auto id : added) { visualizations_.remove(id); captures_.erase(id); }
        graphWarning_ = "Patch not executable; last accepted audio continues. " + text(result.error); message(graphWarning_); return;
    }
    ++revision_; accepted_ = workspace_.graph(); acceptedAudio_ = *compiled.value;
    graphWarning_.clear();
    message("Graph revision " + juce::String(revision_) + " accepted. Processor state resets; old automation events cancel.");
}
void RuntimePanel::parameter(std::uint64_t node, std::uint16_t index, float value, audio::ParameterAction action)
{
    if (!running_) { message("Start transport before changing live parameter layers."); return; }
    const auto result = engine_.setParameter(revision_, node, index, value, action);
    message(result.succeeded ? "Live parameter layer accepted (not saved automation)." : text(result.error));
}
void RuntimePanel::schedule(std::uint64_t node, std::uint16_t index, float value, double seconds)
{
    if (!running_ || !std::isfinite(seconds) || seconds < 0.1 || seconds > 9) { message("Start transport; schedule 0.1-9 seconds ahead."); return; }
    const auto telemetry = engine_.telemetry();
    if (!telemetry.value) { message(text(telemetry.error.message)); return; }
    const auto& t = *telemetry.value;
    const auto result = engine_.scheduleParameter(t.epoch, revision_, node, index,
        t.samplePosition + static_cast<std::uint64_t>(seconds * rate_), value);
    message(result.succeeded ? "Sample-timestamped automation queued; release live override to hear it. Runtime only." : text(result.error));
}
void RuntimePanel::timerCallback()
{
    if (!running_) return;
    if (offline_) engine_.process({nullptr, offlinePointers_.data(), 0, width_, block_});
    const auto collected = engine_.collectRetired();
    if (!collected.succeeded) { message(text(collected.error)); return; }
    const auto query = engine_.telemetry();
    if (!query.value) { message(text(query.error.message)); return; }
    const auto& value = *query.value;
    if (!value.running)
    {
        const auto result = stop();
        message(result.succeeded ? "Audio interrupted/device lost or changed. Stopped; inspect/reselect and restart explicitly." : text(result.error));
        return;
    }
    if (value.activeRevision == revision_)
        for (auto it = captures_.begin(); it != captures_.end();)
            if (!accepted_.nodes.contains(it->first)) { visualizations_.remove(it->first); it = captures_.erase(it); }
            else ++it;
    if (++ticks_ % 15 != 0) return;
    for (const auto& node : acceptedAudio_.nodes)
        if (node.type == audio::AudioNodeType::script)
        {
            const auto fault = engine_.scriptFault(revision_, node.id);
            if (fault.value && fault.value->code != script::FaultCode::none)
            {
                const auto failed = *fault.value;
                const auto stopped = stop();
                message("Script node " + juce::String(node.id) + " fault " + juce::String(static_cast<int>(failed.code))
                    + " at " + juce::String(failed.source.line) + ":" + juce::String(failed.source.column)
                    + (stopped.succeeded ? ". Transport stopped; fix and restart." : ". Stop failed: " + text(stopped.error)));
                return;
            }
        }
    const auto latency = value.reportedOutputLatencySeconds ? juce::String(*value.reportedOutputLatencySeconds * 1000, 2) + " ms reported out"
                                                            : juce::String("latency unavailable");
    const auto inputLatency = value.reportedInputLatencySeconds ? juce::String(*value.reportedInputLatencySeconds * 1000, 2) + " ms in"
                                                               : juce::String("input latency unavailable");
    message((graphWarning_.isEmpty() ? juce::String{} : graphWarning_ + " | ") + (offline_ ? "Offline" : api_.getText() + " | " + output_.getText()) + " | " + juce::String(rate_, 0) + " Hz | "
        + juce::String(block_) + " frames | " + latency + ", " + inputLatency + " | DSP " + juce::String(value.lastCallbackLoad * 100, 1)
        + "% | CPU unavailable | xruns " + (value.backendXruns ? juce::String(*value.backendXruns) : juce::String("unavailable"))
        + " | callback " + juce::String(static_cast<double>(value.callbackNanoseconds) / 1000, 1) + " us"
        + " | misses " + juce::String(value.deadlineMisses) + " | events missed/cancelled " + juce::String(value.missedEvents)
        + "/" + juce::String(value.canceledEvents) + " | numerical faults " + juce::String(value.latestPlanNumericalFaults)
        + " | observing " + juce::String(static_cast<int>(captures_.size()))
        + " sources (max 8, first 2 channels)");
}
}
