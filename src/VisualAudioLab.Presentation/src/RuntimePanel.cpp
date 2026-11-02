#include <val/presentation/RuntimePanel.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>
#include <val/domain/Routing.h>
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
    if (left.connections != right.connections || left.nodes.size() != right.nodes.size()
        || left.endpoints != right.endpoints) return false;
    for (const auto& [id, node] : left.nodes)
    {
        const auto found = right.nodes.find(id);
        if (found == right.nodes.end() || node.parameters != found->second.parameters) return false;
    }
    return true;
}
bool hasZeroInputSourceOnOutput(const audio::AudioGraph& graph)
{
    const auto dependencies = graph.outputDependencies();
    for (const auto& node : graph.nodes)
    {
        const auto generated = node.type == audio::AudioNodeType::signalGenerator
            || node.type == audio::AudioNodeType::impulseGenerator
            || node.type == audio::AudioNodeType::clickGenerator
            || node.type == audio::AudioNodeType::burstGenerator
            || node.type == audio::AudioNodeType::chirpGenerator
            || node.type == audio::AudioNodeType::noiseGenerator
            || node.type == audio::AudioNodeType::dcGenerator
            || (node.type == audio::AudioNodeType::script && node.script && node.script->inputs().empty());
        if (generated && std::binary_search(dependencies.begin(), dependencies.end(), node.id))
            return true;
    }
    return false;
}
}
RuntimePanel::RuntimePanel(WorkspaceService& workspace, VisualizationSession& visualizations, BackendFactory factory)
    : workspace_(workspace), visualizations_(visualizations), factory_(std::move(factory))
{
#if JUCE_WINDOWS
    const auto platform = AudioPlatform::windows;
#elif JUCE_MAC
    const auto platform = AudioPlatform::macOS;
#else
    const auto platform = AudioPlatform::other;
#endif
    const juce::String backendNames[]{"ASIO", "WASAPI Exclusive", "WASAPI Shared", "CoreAudio", "DirectSound"};
    for (auto backend : supportedAudioBackends(platform))
        api_.addItem(backendNames[static_cast<int>(backend)], static_cast<int>(backend) + 1);
    api_.addItem("Offline preview (no hardware)", 6);
#if JUCE_MAC
    api_.setSelectedId(4, juce::dontSendNotification);
#elif JUCE_WINDOWS
    api_.setSelectedId(1, juce::dontSendNotification);
#else
    api_.setSelectedId(6, juce::dontSendNotification);
#endif
    for (auto* control : std::initializer_list<juce::Component*>{&api_, &input_, &output_, &rateChoice_, &buffer_,
             &scan_, &panel_, &start_, &stop_, &done_, &notice_, &state_}) addAndMakeVisible(control);
    const juce::String names[]{"Backend", "Input device", "Output device", "Sample rate", "Buffer frames"};
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_};
    for (int i = 0; i < 5; ++i)
    {
        labels_[i].setText(names[i], juce::dontSendNotification); addAndMakeVisible(labels_[i]);
        boxes[i]->setTitle(names[i]); boxes[i]->setTextWhenNothingSelected("Select explicitly");
    }
    notice_.setText("Assign exact named channels in the fixed canvas lanes. Input is optional; output rows use disjoint channels. "
                    "No automatic playback or device fallback. Check levels before Start; no master limiter.", juce::dontSendNotification);
    notice_.setFont(Typography::ui(13));
    state_.setText(lastStatus_, juce::dontSendNotification); state_.setFont(Typography::ui(13));
    state_.setMinimumHorizontalScale(1.0f);
    state_.setName("Audio setup state");
    state_.setJustificationType(juce::Justification::topLeft);
    api_.onChange = [this] { selectBackend(); };
    scan_.onClick = [this] { scan(); };
    panel_.onClick = [this] { controlPanel(); };
    input_.onChange = [this] { selectionChanged(); };
    output_.onChange = [this] { selectionChanged(true); };
    for (auto* box : {&rateChoice_, &buffer_})
        box->onChange = [this] { configurationChanged(); };
    start_.onClick = [this] { const auto result = start(); if (!result.succeeded) message(text(result.error)); };
    stop_.onClick = [this] { const auto result = stop(); if (!result.succeeded) message(text(result.error)); };
    done_.onClick = [this]
    {
        message("Audio Setup closed. Transport unchanged.");
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setVisible(false);
        else if (onDismissSetup)
            onDismissSetup();
    };
    clearConfiguration("Choose endpoints");
    labels_[1].setText(api_.getSelectedId() == 1 ? "ASIO input" : "Input device", juce::dontSendNotification);
    labels_[2].setText(api_.getSelectedId() == 1 ? "ASIO driver" : "Output device", juce::dontSendNotification);
    input_.setTextWhenNothingSelected("Discovering devices...");
    output_.setTextWhenNothingSelected("Discovering devices...");
    selectBackend();
    startTimerHz(30);
    enableControls();
}
RuntimePanel::~RuntimePanel()
{
    stopTimer(); onStatus = nullptr; onStarted = nullptr; onDismissSetup = nullptr;
    const auto stopped = stop(); juce::ignoreUnused(stopped);
    if (backend_) { const auto result = backend_->shutdown(); juce::ignoreUnused(result); }
}
void RuntimePanel::message(juce::String value)
{
    lastStatus_ = std::move(value); state_.setText(lastStatus_, juce::dontSendNotification);
    state_.setTooltip(lastStatus_);
    if (onStatus) onStatus(lastStatus_);
}
void RuntimePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    auto buttons = bounds.removeFromTop(30);
    start_.setBounds(buttons.removeFromLeft(70).reduced(2)); stop_.setBounds(buttons.removeFromLeft(70).reduced(2));
    done_.setBounds(buttons.removeFromRight(70).reduced(2));
    scan_.setBounds(buttons.removeFromLeft(110).reduced(2));
    panel_.setBounds(buttons.removeFromLeft(110).reduced(2));
    state_.setBounds(bounds.removeFromTop(30).reduced(2));
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_};
    // Two compact columns fit the resizable Audio Setup dialog.
    for (int i = 0; i < 5; ++i)
    {
        const int row = i / 2, col = i % 2, column = bounds.getWidth() / 2;
        auto cell = juce::Rectangle<int>(bounds.getX() + col * column, bounds.getY() + row * 28, column, 28).reduced(2);
        labels_[i].setBounds(cell.removeFromLeft(100)); boxes[i]->setBounds(cell);
    }
    bounds.removeFromTop(84);
    notice_.setBounds(bounds.removeFromTop(52));
}
void RuntimePanel::enableControls()
{
    const bool editable = !running_ && !probing_ && !scanning_;
    api_.setEnabled(!running_);
    input_.setEnabled(!running_ && !scanning_ && !offline_ && input_.getNumItems() > 0
                      && (api_.getSelectedId() != 1 || output_.getSelectedId() > 0));
    output_.setEnabled(!running_ && !scanning_ && !offline_ && output_.getNumItems() > 0);
    for (auto* box : {&rateChoice_, &buffer_})
        box->setEnabled(editable && box->getNumItems() > 0);
    scan_.setEnabled(editable && !offline_ && static_cast<bool>(factory_));
    panel_.setEnabled(editable && outputCaps_ && outputCaps_->hasControlPanel);
    start_.setEnabled(editable && request().value.has_value());
    stop_.setEnabled(running_);
    if (onStatus) onStatus(lastStatus_);
}
void RuntimePanel::clearConfiguration(juce::String placeholder)
{
    inputCaps_.reset(); outputCaps_.reset();
    for (auto* box : {&rateChoice_, &buffer_})
    {
        box->clear(juce::dontSendNotification);
        box->setTextWhenNothingSelected(placeholder);
        box->setTooltip(placeholder);
    }
}
void RuntimePanel::configurationChanged()
{
    const auto configuration = request();
    if (!configuration.value) message(text(configuration.error.message));
    else message(offline_ ? "Offline preview selected. Channel rows remain visible; Start is explicit."
        : "Device and stream format selected. Assign exact channel names in the canvas lanes; Start remains explicit.");
    enableControls();
}
void RuntimePanel::selectionChanged(bool outputChanged)
{
    ++probeSerial_; probing_ = false;
    if (api_.getSelectedId() == 1 && outputChanged)
    {
        const auto index = output_.getSelectedId() - 1;
        input_.clear(juce::dontSendNotification);
        input_.addItem("None (output only)", 1);
        if (index >= 0 && index < static_cast<int>(outputs_.size()))
        {
            input_.addItem("Same ASIO driver (duplex)", index + 2);
            input_.setSelectedId(index + 2, juce::dontSendNotification);
        }
        else input_.setSelectedId(1, juce::dontSendNotification);
    }
    if (output_.getSelectedId() > 0)
        endpointHint_ = AudioSetupPreference{static_cast<AudioBackendKind>(api_.getSelectedId() - 1),
            input_.getSelectedId() > 1 ? inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).name : std::string{},
            outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).name};
    else endpointHint_.reset();
    clearConfiguration("Loading ports...");
    if (output_.getSelectedId() > 0) inspect();
    else message("Select output explicitly. Playback remains stopped.");
    enableControls();
}
void RuntimePanel::selectBackend()
{
    if (running_) return;
    ++probeSerial_; probing_ = false; scanning_ = false;
    endpointHint_.reset();
    if (backend_) { const auto result = backend_->shutdown(); if (!result.succeeded()) message(text(result.message)); backend_.reset(); }
    inputs_.clear(); outputs_.clear(); input_.clear(juce::dontSendNotification); output_.clear(juce::dontSendNotification);
    clearConfiguration("Discovering...");
    labels_[1].setText(api_.getSelectedId() == 1 ? "ASIO input" : "Input device", juce::dontSendNotification);
    labels_[2].setText(api_.getSelectedId() == 1 ? "ASIO driver" : "Output device", juce::dontSendNotification);
    input_.setTextWhenNothingSelected("Discovering devices..."); output_.setTextWhenNothingSelected("Discovering devices...");
    offline_ = api_.getSelectedId() == 6;
    if (offline_) configureOffline(2);
    else scan();
    enableControls();
}
void RuntimePanel::scan()
{
    if (running_ || probing_ || scanning_ || offline_ || !factory_) { message("Stop transport and wait for inspection before scanning a native backend."); return; }
    preference_ = setupPreference();
    if (!backend_)
    {
        backend_ = factory_(static_cast<AudioBackendKind>(api_.getSelectedId() - 1));
        if (!backend_) { message("Selected backend is unavailable in this host."); return; }
        const auto listen = backend_->setChangeCallback([safe = juce::Component::SafePointer<RuntimePanel>(this)](const auto& snapshot)
        { if (safe) safe->inventory(snapshot); });
        if (!listen.succeeded()) { message(text(listen.message)); return; }
    }
    scanning_ = true;
    message("Scanning selected backend...");
    const auto result = backend_->refresh();
    if (!result.succeeded()) { scanning_ = false; clearConfiguration("Discovery failed: retry"); message(text(result.message)); }
    enableControls();
}
void RuntimePanel::inventory(const AudioBackendSnapshot& snapshot)
{
    const auto hint = endpointHint_ ? endpointHint_ : setupPreference();
    const bool interruptedInspection = probing_;
    ++probeSerial_; probing_ = false; scanning_ = false;
    if (running_)
    {
        const auto result = stop();
        if (!result.succeeded) { message(text(result.error)); return; }
        message("Device inventory changed. Transport stopped; revalidate before explicit Start.");
    }
    clearConfiguration("Choose endpoints"); inputs_.clear(); outputs_.clear();
    input_.clear(juce::dontSendNotification); output_.clear(juce::dontSendNotification);
    input_.addItem("None (output only)", 1); input_.setSelectedId(1, juce::dontSendNotification);
    for (const auto& device : snapshot.devices)
    {
        if (device.direction != AudioDirection::output) { inputs_.push_back(device); input_.addItem(text(device.name), static_cast<int>(inputs_.size()) + 1); }
        if (device.direction != AudioDirection::input) { outputs_.push_back(device); output_.addItem(text(device.name), static_cast<int>(outputs_.size())); }
    }
    if (snapshot.backend == AudioBackendKind::asio)
    {
        input_.clear(juce::dontSendNotification);
        input_.addItem("None (output only)", 1);
        input_.setSelectedId(1, juce::dontSendNotification);
    }
    bool restored{};
    if (hint && hint->backend == snapshot.backend)
    {
        const auto selectNamed = [](juce::ComboBox& box, const auto& devices, const std::string& name, int offset)
        {
            auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& device) { return device.name == name; });
            if (found == devices.end() || std::count_if(devices.begin(), devices.end(), [&](const auto& device) { return device.name == name; }) != 1)
                return false;
            box.setSelectedId(static_cast<int>(found - devices.begin()) + offset, juce::dontSendNotification);
            return true;
        };
        restored = selectNamed(output_, outputs_, hint->outputName, 1);
        if (snapshot.backend == AudioBackendKind::asio && restored)
            input_.addItem("Same ASIO driver (duplex)", output_.getSelectedId() + 1);
        if (!hint->inputName.empty())
            restored = selectNamed(input_, inputs_, hint->inputName, 2) && restored;
        else input_.setSelectedId(1, juce::dontSendNotification);
        if (!restored)
        {
            output_.setSelectedId(0, juce::dontSendNotification);
            input_.setSelectedId(1, juce::dontSendNotification);
        }
    }
    input_.setTextWhenNothingSelected(inputs_.empty() ? "No input endpoints reported" : "Select input or None");
    output_.setTextWhenNothingSelected(outputs_.empty() ? "No outputs: connect, then Retry" : "Select output explicitly");
    if (!snapshot.error.succeeded()) message(text(snapshot.error.message));
    else if (outputs_.empty()) message("No output devices reported. Connect a device/install its driver, then Refresh / Retry; choose another supported backend explicitly if needed.");
    else if (hint && hint->backend == snapshot.backend)
        message(restored ? "Saved devices restored. Revalidating ports; playback remains stopped."
                         : "A saved device is missing or ambiguous. Select available devices; no replacement was selected.");
    else message(snapshot.backend == AudioBackendKind::asio
        ? "Select an ASIO driver. Ports load automatically; one driver serves both directions."
        : "Select input/output devices. Ports load automatically; Start remains explicit.");
    if (interruptedInspection && snapshot.error.succeeded() && !outputs_.empty())
        message("Device inventory changed during inspection; its result was discarded.");
    if (snapshot.error.succeeded() && output_.getSelectedId() > 0) inspect();
    enableControls();
}
void RuntimePanel::inspect()
{
    if (!backend_ || running_ || scanning_ || probing_ || output_.getSelectedId() < 1) { message("Select an output device and wait for scanning first."); return; }
    clearConfiguration("Inspecting..."); probing_ = true;
    const auto serial = ++probeSerial_;
    auto safe = juce::Component::SafePointer<RuntimePanel>(this);
    std::optional<AudioDeviceToken> input;
    if (input_.getSelectedId() > 1) input = inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).token;
    message("Loading device options... Automatic driver inspection; playback is stopped. Refresh / Retry after a driver failure.");
    const auto result = backend_->queryConfigurationCapabilities(input, outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token,
        [safe, serial](auto query)
        {
            if (!safe || safe->probeSerial_ != serial) return;
            safe->capabilities(std::move(query));
        });
    if (!result.succeeded())
    {
        probing_ = false; clearConfiguration("Request rejected: retry");
        message("Device options request rejected: " + text(result.message));
    }
    enableControls();
}
void RuntimePanel::capabilities(AudioQuery<AudioDeviceCapabilities> result)
{
    if (!result.value)
    {
        probing_ = false; clearConfiguration("Inspection failed: retry");
        message("Device options failed: " + text(result.error.message) + " Use Refresh / Retry after checking device setup.");
        enableControls(); return;
    }
    outputCaps_ = std::move(*result.value);
    if (input_.getSelectedId() > 1) inputCaps_ = outputCaps_;
    probing_ = false; populateConfiguration(); enableControls();
}
void RuntimePanel::populateConfiguration()
{
    for (auto* box : {&rateChoice_, &buffer_}) box->clear(juce::dontSendNotification);
    if (outputCaps_)
    {
        int id = 1;
        for (const auto rate : outputCaps_->sampleRates)
            if (std::isfinite(rate) && rate >= 8000 && rate <= 384000)
                rateChoice_.addItem(juce::String(rate, 0), id++);
        id = 1;
        for (const auto frames : outputCaps_->bufferFrames)
            if (frames > 0 && frames <= 4096)
                buffer_.addItem(juce::String(frames), id++);
    }
    if (rateChoice_.getNumItems() == 1) rateChoice_.setSelectedId(1, juce::dontSendNotification);
    if (buffer_.getNumItems() == 1) buffer_.setSelectedId(1, juce::dontSendNotification);
    for (auto* box : {&rateChoice_, &buffer_})
        box->setTextWhenNothingSelected(box->getNumItems() == 0 ? "Not reported: retry" : "Select reported choice");
    const bool shared = api_.getSelectedId() == 3;
    labels_[4].setText(shared ? "Callback frames" : "Buffer frames", juce::dontSendNotification);
    buffer_.setTooltip(shared
        ? "JUCE callback block choices. Windows manages the hardware period; these are not hardware buffer-size controls."
        : "Driver/JUCE-reported candidates. A single reported value is fixed for this inspection; empty means unknown. Open must confirm the selected size.");
    bool restored = false;
    if (preference_ && static_cast<int>(preference_->backend) + 1 == api_.getSelectedId()
        && preference_->outputName == output_.getText().toStdString()
        && preference_->inputName == (input_.getSelectedId() > 1
            ? inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).name : std::string{}))
    {
        const auto selectText = [](juce::ComboBox& box, juce::String value)
        {
            for (int i = 0; i < box.getNumItems(); ++i)
                if (box.getItemText(i) == value) { box.setSelectedId(box.getItemId(i), juce::dontSendNotification); return true; }
            return false;
        };
        restored = selectText(rateChoice_, juce::String(preference_->sampleRate, 0));
        restored = selectText(buffer_, juce::String(preference_->bufferFrames)) && restored;
    }
    message(juce::String(outputCaps_->outputChannels.empty()
                ? "No output channels reported. Refresh / Retry or check vendor setup; Start is unavailable. "
                : rateChoice_.getNumItems() == 0 || buffer_.getNumItems() == 0
                ? "Rate/buffer candidates are unreported or outside engine limits; Start is unavailable. Refresh / Retry, check vendor/OS setup, or select different endpoints/backend. "
                : restored ? "Saved settings revalidated. Start remains explicit. "
                           : "Capabilities ready. Assign named channels to endpoint rows in the canvas lanes. ")
            + (shared ? "Windows manages the hardware period; callback frames are application block choices. " : "")
            + (buffer_.getNumItems() == 1 ? "One buffer value reported for this configuration. " : ""));
}
void RuntimePanel::configureOffline(int channels, double rate, int frames)
{
    if (running_) return;
    ++probeSerial_; probing_ = false; scanning_ = false;
    if (backend_) { const auto result = backend_->shutdown(); juce::ignoreUnused(result); backend_.reset(); }
    inputs_.clear(); outputs_.clear(); input_.clear(juce::dontSendNotification); output_.clear(juce::dontSendNotification);
    endpointHint_.reset();
    offline_ = true; api_.setSelectedId(6, juce::dontSendNotification);
    for (auto* box : {&rateChoice_, &buffer_}) box->clear(juce::dontSendNotification);
    width_ = channels;
    rateChoice_.addItem(juce::String(rate, 0), 1); rateChoice_.setSelectedId(1, juce::dontSendNotification);
    buffer_.addItem(juce::String(frames), 1); buffer_.setSelectedId(1, juce::dontSendNotification);
    message("Offline preview selected. Start is explicit; no device opens.");
    enableControls();
}
void RuntimePanel::restoreSetup(std::optional<AudioSetupPreference> preference)
{
    if (running_) return;
    preference_ = std::move(preference);
    if (!preference_) return;
    const auto validation = validateSettings(Settings{1100, 720, true, preference_});
    if (!validation.succeeded) { message(text(validation.error)); preference_.reset(); return; }
    const int id = static_cast<int>(preference_->backend) + 1;
    if (api_.indexOfItemId(id) < 0)
    {
        message("Saved backend is unsupported on this OS. Choose a supported backend explicitly; no saved device was substituted.");
        enableControls();
        return;
    }
    api_.setSelectedId(id, juce::dontSendNotification);
    selectBackend();
}
std::optional<AudioSetupPreference> RuntimePanel::setupPreference() const
{
    if (!offline_)
    {
        const auto configuration = request();
        if (configuration.value)
        {
            const auto& value = configuration.value->stream;
            auto saved = AudioSetupPreference{static_cast<AudioBackendKind>(api_.getSelectedId() - 1),
                input_.getSelectedId() > 1 ? inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).name : std::string{},
                outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).name,
                value.sampleRate, value.bufferFrames, value.inputChannels, value.outputChannels,
                value.inputChannelIndices, value.outputChannelIndices};
            if (preference_) saved.endpointChannelIndices = preference_->endpointChannelIndices;
            return saved;
        }
    }
    return preference_;
}
std::vector<std::string> RuntimePanel::endpointChannelNames(domain::EndpointRole role) const
{
    const auto& caps = role == domain::EndpointRole::hardwareInput ? inputCaps_ : outputCaps_;
    if (!caps) return {};
    return role == domain::EndpointRole::hardwareInput ? caps->inputChannels : caps->outputChannels;
}
std::vector<int> RuntimePanel::selectedEndpointChannelIndices(domain::GraphId id) const
{
    const auto endpoint = workspace_.graph().endpoints.find(id);
    if (endpoint == workspace_.graph().endpoints.end() || endpoint->second.channelNames.empty()) return {};
    if (preference_)
        if (const auto found = preference_->endpointChannelIndices.find(id);
            found != preference_->endpointChannelIndices.end()
            && found->second.size() == endpoint->second.channelNames.size())
            return found->second;
    const auto available = endpointChannelNames(endpoint->second.role);
    std::vector<int> result;
    for (const auto& name : endpoint->second.channelNames)
    {
        const auto found = std::find(available.begin(), available.end(), name);
        if (found == available.end() || std::count(available.begin(), available.end(), name) != 1) return {};
        result.push_back(static_cast<int>(found - available.begin()));
    }
    return result;
}
Result RuntimePanel::assignEndpointChannels(domain::GraphId id, const std::vector<int>& channelIndices)
{
    const auto endpoint = workspace_.graph().endpoints.find(id);
    if (endpoint == workspace_.graph().endpoints.end())
        return Result::failure("Hardware endpoint row no longer exists.");
    if (channelIndices.empty())
    {
        const auto result = workspace_.editHardwareEndpoint(id, endpoint->second.name, endpoint->second.enabled,
                                                              endpoint->second.layoutChannels, {});
        if (result.succeeded && preference_) preference_->endpointChannelIndices.erase(id);
        return result;
    }
    if (!application::validAudioChannelSelection(channelIndices, static_cast<int>(channelIndices.size())))
        return Result::failure("Select one or more distinct driver channels in ascending channel order.");
    const auto names = endpointChannelNames(endpoint->second.role);
    if (names.empty())
        return Result::failure(endpoint->second.role == domain::EndpointRole::hardwareInput
            ? "Select and inspect an input device before assigning its channels."
            : "Select and inspect an output device before assigning its channels.");
    if (channelIndices.back() >= static_cast<int>(names.size()))
        return Result::failure("One or more selected driver channels are no longer available; refresh device options.");
    std::vector<std::string> selectedNames;
    selectedNames.reserve(channelIndices.size());
    for (const auto index : channelIndices)
        selectedNames.push_back(names[static_cast<std::size_t>(index)]);
    const auto layoutChannels = static_cast<int>(selectedNames.size());
    const auto result = workspace_.editHardwareEndpoint(id, endpoint->second.name, true,
        layoutChannels, std::move(selectedNames));
    if (result.succeeded)
    {
        if (!preference_) preference_.emplace();
        preference_->endpointChannelIndices[id] = channelIndices;
    }
    return result;
}
juce::String RuntimePanel::endpointReadiness(domain::GraphId id) const
{
    const auto endpoint = workspace_.graph().endpoints.find(id);
    if (endpoint == workspace_.graph().endpoints.end()) return "Unavailable";
    if (!endpoint->second.enabled) return "Disabled";
    if (endpoint->second.channelNames.empty()) return "Unassigned";
    if (offline_) return "Offline preview · " + juce::String(endpoint->second.layoutChannels) + " channels";
    const auto& caps = endpoint->second.role == domain::EndpointRole::hardwareInput ? inputCaps_ : outputCaps_;
    if (!caps) return "Device not ready";
    const auto& available = endpoint->second.role == domain::EndpointRole::hardwareInput
        ? caps->inputChannels : caps->outputChannels;
    const std::vector<int>* stored{};
    if (preference_)
        if (const auto found = preference_->endpointChannelIndices.find(id);
            found != preference_->endpointChannelIndices.end()) stored = &found->second;
    for (std::size_t channel = 0; channel < endpoint->second.channelNames.size(); ++channel)
    {
        if (stored)
        {
            if (stored->size() != endpoint->second.channelNames.size())
                return "Missing · reassign channels";
            const auto index = (*stored)[channel];
            if (index < 0 || index >= static_cast<int>(available.size())
                || available[static_cast<std::size_t>(index)] != endpoint->second.channelNames[channel])
                return "Missing · channel identity changed";
        }
        else if (std::count(available.begin(), available.end(), endpoint->second.channelNames[channel]) != 1)
            return "Missing or ambiguous · reassign";
    }
    return "Ready · " + juce::String(endpoint->second.layoutChannels) + " ch";
}
juce::String RuntimePanel::compactStatus() const
{
    if (running_) return offline_ ? "Offline running" : "Audio running";
    if (scanning_) return "Audio: scanning";
    if (probing_) return "Audio: inspecting";
    if (!offline_ && backend_ && output_.getSelectedId() > 0 && !outputCaps_)
        return "Audio: load device options (stopped)";
    return request().value ? "Audio ready (stopped)" : "Audio setup required (stopped)";
}
AudioQuery<RuntimePanel::ResolvedAudioSetup> RuntimePanel::request() const
{
    if (probing_ || scanning_) return {{}, invalid("Wait for discovery/inspection to finish.")};
    ResolvedAudioSetup resolved;
    auto& value = resolved.stream;
    if (rateChoice_.getSelectedId() < 1 || buffer_.getSelectedId() < 1)
        return {{}, invalid("Select a reported sample rate and buffer size.")};
    value.sampleRate = rateChoice_.getText().getDoubleValue(); value.bufferFrames = buffer_.getText().getIntValue();
    value.maxFrames = std::max(512, value.bufferFrames);
    if (offline_)
    {
        const auto hasLegacyDestination = std::any_of(workspace_.graph().nodes.begin(), workspace_.graph().nodes.end(),
            [](const auto& item) { return domain::audioType(item.second) == "Output"; });
        if (hasLegacyDestination)
        {
            value.outputChannels = width_;
            value.outputChannelIndices = legacyAudioChannels(width_);
        }
        else
        {
            int nextSlot{};
            bool firstOutput = true;
            for (const auto& [id, endpoint] : workspace_.graph().endpoints)
                if (endpoint.role == domain::EndpointRole::hardwareOutput && endpoint.enabled)
                {
                    const int channels = firstOutput ? width_ : endpoint.layoutChannels;
                    firstOutput = false;
                    if (channels < 1 || channels > 32 || nextSlot + channels > 32)
                        return {{}, invalid("Offline preview output rows exceed the 32-channel limit.")};
                    auto& map = resolved.endpointChannelMaps[id];
                    for (int channel = 0; channel < channels; ++channel)
                        map.push_back(nextSlot + channel);
                    nextSlot += channels;
                }
            if (nextSlot == 0)
                return {{}, invalid("Enable at least one output row for offline preview.")};
            value.outputChannels = nextSlot;
            value.outputChannelIndices = legacyAudioChannels(nextSlot);
        }
        value.inputChannels = 0;
    }
    else
    {
        if (!outputCaps_ || output_.getSelectedId() < 1 || !backend_)
            return {{}, invalid("Select a native output device and wait for device inspection.")};
        value.output = outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token;
        std::set<int> inputChannels, outputChannels;
        std::map<int, std::string> outputOwners;
        bool hasInputRows{};
        for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        {
            if (!endpoint.enabled || endpoint.channelNames.empty()) continue;
            const bool inputRole = endpoint.role == domain::EndpointRole::hardwareInput;
            const auto* caps = inputRole ? (input_.getSelectedId() > 1 ? &inputCaps_ : nullptr) : &outputCaps_;
            if (!caps || !*caps)
                return {{}, invalid(inputRole
                    ? "Input rows are assigned but no input device is selected and inspected."
                    : "An output row is assigned but no output device is selected and inspected.")};
            const auto& names = inputRole ? (*caps)->inputChannels : (*caps)->outputChannels;
            const std::vector<int>* stored{};
            if (preference_)
                if (const auto found = preference_->endpointChannelIndices.find(id);
                    found != preference_->endpointChannelIndices.end()) stored = &found->second;
            std::vector<int> physical;
            for (std::size_t channel = 0; channel < endpoint.channelNames.size(); ++channel)
            {
                int index = -1;
                if (stored)
                {
                    if (stored->size() != endpoint.channelNames.size())
                        return {{}, invalid("Saved channel binding width changed for " + endpoint.name + "; reassign the row.")};
                    index = (*stored)[channel];
                    if (index < 0 || index >= static_cast<int>(names.size())
                        || names[static_cast<std::size_t>(index)] != endpoint.channelNames[channel])
                        return {{}, invalid("Saved driver channel identity changed for " + endpoint.name + "; reassign the row.")};
                }
                else
                {
                    const auto matches = std::find(names.begin(), names.end(), endpoint.channelNames[channel]);
                    if (matches == names.end()
                        || std::count(names.begin(), names.end(), endpoint.channelNames[channel]) != 1)
                        return {{}, invalid("Channel name is missing or ambiguous for " + endpoint.name + "; reassign the row.")};
                    index = static_cast<int>(matches - names.begin());
                }
                physical.push_back(index);
            }
            if (!application::validAudioChannelSelection(physical, static_cast<int>(physical.size())))
                return {{}, invalid("Channel assignments must be unique and in driver order for " + endpoint.name + ".")};
            auto& unionChannels = inputRole ? inputChannels : outputChannels;
            for (const auto index : physical)
            {
                if (!inputRole)
                {
                    if (const auto owner = outputOwners.find(index); owner != outputOwners.end())
                        return {{}, invalid("Output channel " + juce::String(index + 1).toStdString() + " is assigned to both "
                            + owner->second + " and " + endpoint.name + "; output rows must use disjoint channels.")};
                    outputOwners.emplace(index, endpoint.name);
                }
                unionChannels.insert(index);
            }
            if (inputRole) hasInputRows = true;
            (void) id;
        }
        if (outputChannels.empty())
            return {{}, invalid("Assign at least one named channel to an enabled output row before Start.")};
        if (outputChannels.size() > 32 || inputChannels.size() > 32)
            return {{}, invalid("Configured input/output channel groups exceed the 32-channel runtime limit.")};
        value.outputChannelIndices.assign(outputChannels.begin(), outputChannels.end());
        value.inputChannelIndices.assign(inputChannels.begin(), inputChannels.end());
        value.outputChannels = static_cast<int>(value.outputChannelIndices.size());
        value.inputChannels = static_cast<int>(value.inputChannelIndices.size());
        const auto compactMap = [](const std::vector<int>& physical, const std::set<int>& packed)
        {
            std::vector<int> result;
            for (const auto index : physical)
                result.push_back(static_cast<int>(std::distance(packed.begin(), packed.find(index))));
            return result;
        };
        for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        {
            if (!endpoint.enabled || endpoint.channelNames.empty()) continue;
            const bool inputRole = endpoint.role == domain::EndpointRole::hardwareInput;
            const auto* caps = inputRole ? (input_.getSelectedId() > 1 ? &inputCaps_ : nullptr) : &outputCaps_;
            if (!caps || !*caps) continue;
            const auto& names = inputRole ? (*caps)->inputChannels : (*caps)->outputChannels;
            const std::vector<int>* stored{};
            if (preference_)
                if (const auto found = preference_->endpointChannelIndices.find(id);
                    found != preference_->endpointChannelIndices.end()) stored = &found->second;
            std::vector<int> physical;
            for (std::size_t channel = 0; channel < endpoint.channelNames.size(); ++channel)
            {
                const auto found = stored ? (*stored)[channel]
                    : static_cast<int>(std::find(names.begin(), names.end(), endpoint.channelNames[channel]) - names.begin());
                physical.push_back(found);
            }
            resolved.endpointChannelMaps.emplace(id, compactMap(physical, inputRole ? inputChannels : outputChannels));
        }
        if (hasInputRows)
        {
            if (!inputCaps_ || input_.getSelectedId() < 2)
                return {{}, invalid("Input rows are assigned but no input device is selected.")};
            value.input = inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).token;
        }
    }
    if (value.sampleRate < 8000 || value.sampleRate > 384000 || value.bufferFrames < 1 || value.bufferFrames > 4096
        || value.outputChannels < 1 || value.outputChannels > 32 || value.inputChannels < 0 || value.inputChannels > 32)
        return {{}, invalid("Start unavailable: select 1-32 output channels and no more than 32 input channels, plus a reported rate/buffer.")};
    if (!offline_)
    {
        if (value.inputChannels != 0 && !value.input)
            return {{}, invalid("Input channels require an explicitly selected input device.")};
    }
    return {std::move(resolved), {}};
}
int RuntimePanel::channels() const noexcept
{
    const auto configuration = request();
    return configuration.value ? configuration.value->stream.outputChannels : 2;
}
void RuntimePanel::controlPanel()
{
    if (!panel_.isEnabled() || !backend_ || output_.getSelectedId() < 1) return;
    probing_ = true;
    const auto serial = ++probeSerial_;
    message("Driver Control Panel open. Close it to revalidate ports; playback remains stopped.");
    const auto result = backend_->showControlPanel(outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token,
        [safe = juce::Component::SafePointer<RuntimePanel>(this), serial](AudioError error)
        {
            if (!safe || safe->probeSerial_ != serial) return;
            safe->probing_ = false;
            if (!error.succeeded()) { safe->message(text(error.message)); safe->enableControls(); }
            else safe->scan();
        });
    if (!result.succeeded()) { probing_ = false; message(text(result.message)); }
    enableControls();
}
AudioQuery<audio::AudioGraph> RuntimePanel::compileGraph(
    bool captures, const std::map<std::uint64_t, std::vector<int>>& endpointChannelMaps)
{
    auto compiled = audio::AudioGraph::fromStructural(workspace_.graph(), width_, endpointChannelMaps);
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
    const auto dependencies = graph.outputDependencies();
    for (const auto& node : graph.nodes)
        if (!node.hardwareEndpoint && node.type == audio::AudioNodeType::analyzer) observed.push_back(node.id);
    for (const auto& node : graph.nodes)
        if (!node.hardwareEndpoint && node.type != audio::AudioNodeType::input
            && node.type != audio::AudioNodeType::analyzer) observed.push_back(node.id);
    for (auto id : observed)
    {
        if (!std::binary_search(dependencies.begin(), dependencies.end(), id)) continue;
        if (captures_.contains(id) || static_cast<int>(captures_.size()) >= limit) continue;
        auto capture = std::make_shared<audio::SignalCaptureBuffer>(id, rate_, captureChannels);
        auto profile = audio::AnalyzerProfile{};
        const auto& parameters = workspace_.graph().nodes.at(id).parameters;
        if (const auto kind = parameters.find("visualization.kind"); kind != parameters.end())
            if (const auto* name = std::get_if<std::string>(&kind->second))
                profile.guitarTuner = *name == "Guitar Tuner";
        const auto registered = visualizations_.add(capture, text(workspace_.graph().nodes.at(id).name), profile);
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
    const auto& resolved = *configuration.value;
    const auto& config = resolved.stream;
    if (!offline_) preference_ = setupPreference();
    width_ = config.outputChannels; rate_ = config.sampleRate; block_ = config.bufferFrames;
    auto compiled = compileGraph(false, resolved.endpointChannelMaps);
    if (!compiled.value) return Result::failure(compiled.error.message);
    if (compiled.value->nodes.empty()) return Result::failure("Patch is empty. Insert and connect a source or processor to Output.");
    const bool outputOnlyWithoutGenerator = config.inputChannels == 0 && !hasZeroInputSourceOnOutput(*compiled.value);
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
    message(outputOnlyWithoutGenerator
        ? "Audio is running with input disabled, but no zero-input source feeds Output; this patch may render silence. Connect a generator or enable input."
        : offline_ ? "Offline preview running (no hardware, not realtime scheduling)." : "Audio running. Stop before changing device configuration.");
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
    if (next.connections != accepted_.connections || next.endpoints != accepted_.endpoints
        || next.nodes.size() != accepted_.nodes.size()) return false;
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
    const auto setup = request();
    if (!setup.value) return Result::failure(setup.error.message);
    const auto mapped = audio::AudioGraph::fromStructural(next, width_, setup.value->endpointChannelMaps);
    if (!mapped.value) return Result::failure(mapped.error.message);
    // Validate all changes before delivering any. Queue overflow is visible, never silently dropped.
    const auto format = engine_.streamFormat();
    if (!format.value) return Result::failure(format.error.message);
    const auto plan = audio::PreparedAudioGraph::prepare(*mapped.value, *format.value, revision_);
    if (!plan.value) return Result::failure(plan.error.message);
    for (const auto& node : mapped.value->nodes)
    {
        if (!next.nodes.contains(node.id)) continue;
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
    if (workspace_.graph().endpoints != accepted_.endpoints)
    {
        const auto result = stop();
        message(result.succeeded
            ? "Hardware endpoint assignments changed. Transport stopped; Start again to open the new exact channel masks."
            : "Endpoint change requires a stream restart, but Stop failed: " + text(result.error));
        return;
    }
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
    const auto setup = request();
    if (!setup.value)
    {
        graphWarning_ = "Patch not executable; last accepted audio continues. " + text(setup.error.message);
        message(graphWarning_);
        return;
    }
    auto compiled = compileGraph(true, setup.value->endpointChannelMaps);
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
        message(result.succeeded ? "Audio interrupted/device lost or changed. Stopped; reselect endpoints and restart explicitly." : text(result.error));
        return;
    }
    if (value.activeRevision == revision_)
    {
        const auto dependencies = acceptedAudio_.outputDependencies();
        for (auto it = captures_.begin(); it != captures_.end();)
            if (!accepted_.nodes.contains(it->first) || !std::binary_search(dependencies.begin(), dependencies.end(), it->first))
            { visualizations_.remove(it->first); it = captures_.erase(it); }
            else ++it;
    }
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
    const auto latency = value.reportedOutputLatencySeconds ? Typography::quantity(*value.reportedOutputLatencySeconds * 1000, 1, "ms") + " reported out"
                                                            : juce::String("latency unavailable");
    const auto inputLatency = value.reportedInputLatencySeconds ? Typography::quantity(*value.reportedInputLatencySeconds * 1000, 1, "ms") + " in"
                                                               : juce::String("input latency unavailable");
    message((graphWarning_.isEmpty() ? juce::String{} : graphWarning_ + " | ") + (offline_ ? "Offline" : api_.getText() + " | " + output_.getText()) + " | " + Typography::sampleRate(rate_) + " | "
        + juce::String(block_) + " samples | " + latency + ", " + inputLatency + " | DSP " + Typography::number(value.lastCallbackLoad * 100, 1)
        + "% | CPU unavailable | xruns " + (value.backendXruns ? juce::String(*value.backendXruns) : juce::String("unavailable"))
        + " | callback " + juce::String(static_cast<double>(value.callbackNanoseconds) / 1000, 1) + " us"
        + " | misses " + juce::String(value.deadlineMisses) + " | events missed/cancelled " + juce::String(value.missedEvents)
        + "/" + juce::String(value.canceledEvents) + " | numerical faults " + juce::String(value.latestPlanNumericalFaults)
        + " | observing " + juce::String(static_cast<int>(captures_.size()))
        + " sources (max 8, first 2 channels)");
}
}
