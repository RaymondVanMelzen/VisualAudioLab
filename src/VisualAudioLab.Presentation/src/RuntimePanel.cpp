#include <val/presentation/RuntimePanel.h>
#include <val/presentation/ShellTheme.h>
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
juce::String previewChoice(int count)
{
    if (count == 0) return "0 channels (input disabled)";
    return juce::String(count) + (count == 1 ? " channel: " : " channels: ") + "preview bus 1"
        + (count > 1 ? "-" + juce::String(count) : "");
}
juce::String portChoice(const std::vector<int>& indices, const std::vector<std::string>& names)
{
    juce::StringArray selected;
    for (int index : indices)
    {
        const auto name = text(names.at(static_cast<std::size_t>(index)));
        selected.add(juce::String(index + 1) + ": " + (name.trim().isEmpty() ? "name not reported" : name));
    }
    return (indices.size() == 1 ? "Mono: " : indices.size() == 2 ? "Stereo: " : juce::String(indices.size()) + "-channel: ")
        + selected.joinIntoString(" / ");
}
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
    for (auto* control : std::initializer_list<juce::Component*>{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_,
             &scan_, &panel_, &advanced_, &start_, &stop_, &done_, &notice_, &state_}) addAndMakeVisible(control);
    const juce::String names[]{"Backend", "Input device", "Output device", "Sample rate", "Buffer frames", "Input port", "Output port"};
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_};
    for (int i = 0; i < 7; ++i)
    {
        labels_[i].setText(names[i], juce::dontSendNotification); addAndMakeVisible(labels_[i]);
        boxes[i]->setTitle(names[i]); boxes[i]->setTextWhenNothingSelected("Select explicitly");
    }
    notice_.setText("Check output levels before Start; no master limiter. Automatic inspection may initialize hardware "
                    "(ASIO may briefly start/stop its driver). Ports use exact driver channel identities. "
                    "Input must be disabled or match output width. No automatic playback or fallback.", juce::dontSendNotification);
    notice_.setFont(juce::FontOptions(13.0f));
    state_.setText(lastStatus_, juce::dontSendNotification); state_.setFont(juce::FontOptions(13.0f));
    state_.setMinimumHorizontalScale(1.0f);
    state_.setName("Audio setup state");
    state_.setJustificationType(juce::Justification::topLeft);
    api_.onChange = [this] { selectBackend(); };
    scan_.onClick = [this] { scan(); };
    panel_.onClick = [this] { controlPanel(); };
    advanced_.onClick = [this]
    {
        if (offline_) return;
        preference_ = setupPreference();
        populateConfiguration();
        enableControls();
    };
    advanced_.setTooltip("Show wider contiguous driver channel groups (up to 32). No arbitrary remapping or engine width conversion.");
    input_.onChange = [this] { selectionChanged(); };
    output_.onChange = [this] { selectionChanged(true); };
    for (auto* box : {&rateChoice_, &buffer_, &inputCount_, &outputCount_})
        box->onChange = [this] { configurationChanged(); };
    start_.onClick = [this] { const auto result = start(); if (!result.succeeded) message(text(result.error)); };
    stop_.onClick = [this] { const auto result = stop(); if (!result.succeeded) message(text(result.error)); };
    done_.onClick = [this] { if (onDismissSetup) onDismissSetup(); };
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
    advanced_.setBounds(bounds.removeFromTop(26).removeFromLeft(160).reduced(2));
    state_.setBounds(bounds.removeFromTop(30).reduced(2));
    juce::ComboBox* boxes[]{&api_, &input_, &output_, &rateChoice_, &buffer_, &inputCount_, &outputCount_};
    // Two compact columns fit the occasional setup dock.
    for (int i = 0; i < 7; ++i)
    {
        const int row = i / 2, col = i % 2, column = bounds.getWidth() / 2;
        auto cell = juce::Rectangle<int>(bounds.getX() + col * column, bounds.getY() + row * 28, column, 28).reduced(2);
        labels_[i].setBounds(cell.removeFromLeft(100)); boxes[i]->setBounds(cell);
    }
    bounds.removeFromTop(112);
    notice_.setBounds(bounds.removeFromTop(52));
}
void RuntimePanel::enableControls()
{
    const bool editable = !running_ && !probing_ && !scanning_;
    api_.setEnabled(!running_);
    input_.setEnabled(!running_ && !scanning_ && !offline_ && input_.getNumItems() > 0
                      && (api_.getSelectedId() != 1 || output_.getSelectedId() > 0));
    output_.setEnabled(!running_ && !scanning_ && !offline_ && output_.getNumItems() > 0);
    for (auto* box : {&rateChoice_, &buffer_, &inputCount_, &outputCount_})
        box->setEnabled(editable && box->getNumItems() > 0);
    scan_.setEnabled(editable && !offline_ && static_cast<bool>(factory_));
    panel_.setEnabled(editable && outputCaps_ && outputCaps_->hasControlPanel);
    advanced_.setEnabled(editable && !offline_ && outputCaps_.has_value());
    start_.setEnabled(editable && request().value.has_value());
    stop_.setEnabled(running_);
    if (onStatus) onStatus(lastStatus_);
}
void RuntimePanel::clearConfiguration(juce::String placeholder)
{
    inputCaps_.reset(); outputCaps_.reset();
    inputPorts_.clear(); outputPorts_.clear();
    for (auto* box : {&rateChoice_, &buffer_, &inputCount_, &outputCount_})
    {
        box->clear(juce::dontSendNotification);
        box->setTextWhenNothingSelected(placeholder);
        box->setTooltip(placeholder);
    }
}
void RuntimePanel::configurationChanged()
{
    updateChannelTooltips();
    const auto configuration = request();
    if (!configuration.value) message(text(configuration.error.message));
    else message(offline_ ? "Preview bus width selected; no hardware ports. Start remains explicit."
        : "Driver ports selected. Start remains explicit; no width conversion or silent fallback.");
    enableControls();
}
void RuntimePanel::updateChannelTooltips()
{
    for (auto* box : {&inputCount_, &outputCount_})
    {
        const auto selection = box->getSelectedId() > 0 ? box->getText() : juce::String("No port selected.");
        box->setTooltip(selection + (offline_
            ? "\nOffline preview bus only; no hardware ports."
            : "\nExact driver indices, packed in ascending order into the engine bus. Driver names are shown verbatim; "
              "physical/virtual classification is not provided by this metadata. Check vendor routing."));
    }
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
    for (auto* box : {&rateChoice_, &buffer_, &inputCount_, &outputCount_}) box->clear(juce::dontSendNotification);
    inputPorts_ = {{}};
    outputPorts_.clear();
    inputCount_.addItem("None (input disabled)", 1);
    const auto appendPorts = [&](juce::ComboBox& box, std::vector<std::vector<int>>& ports, const std::vector<std::string>& names)
    {
        for (int width = 1; width <= (advanced_.getToggleState() ? 32 : 2); ++width)
            for (int first = 0; first + width <= static_cast<int>(names.size()); ++first)
            {
                std::vector<int> indices;
                for (int i = 0; i < width; ++i) indices.push_back(first + i);
                ports.push_back(indices);
                box.addItem(portChoice(indices, names), static_cast<int>(ports.size()));
            }
    };
    if (inputCaps_)
        appendPorts(inputCount_, inputPorts_, inputCaps_->inputChannels);
    if (outputCaps_)
    {
        appendPorts(outputCount_, outputPorts_, outputCaps_->outputChannels);
        int id = 1;
        for (const auto rate : outputCaps_->sampleRates)
            if (std::isfinite(rate) && rate >= 8000 && rate <= 384000)
                rateChoice_.addItem(juce::String(rate, 0), id++);
        id = 1;
        for (const auto frames : outputCaps_->bufferFrames)
            if (frames > 0 && frames <= 4096)
                buffer_.addItem(juce::String(frames), id++);
    }
    inputCount_.setSelectedId(1, juce::dontSendNotification);
    if (outputCount_.getNumItems() > 0)
        outputCount_.setSelectedId(outputCaps_->outputChannels.size() >= 2
            ? static_cast<int>(outputCaps_->outputChannels.size()) + 1 : 1, juce::dontSendNotification);
    if (rateChoice_.getNumItems() == 1) rateChoice_.setSelectedId(1, juce::dontSendNotification);
    if (buffer_.getNumItems() == 1) buffer_.setSelectedId(1, juce::dontSendNotification);
    for (auto* box : {&rateChoice_, &buffer_, &outputCount_})
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
        const auto selectPorts = [](juce::ComboBox& box, const auto& ports, const std::vector<int>& selected)
        {
            const auto found = std::find(ports.begin(), ports.end(), selected);
            box.setSelectedId(found == ports.end() ? 0 : static_cast<int>(found - ports.begin()) + 1, juce::dontSendNotification);
            return found != ports.end();
        };
        restored = selectPorts(outputCount_, outputPorts_, preference_->outputChannelIndices) && restored;
        restored = selectPorts(inputCount_, inputPorts_, preference_->inputChannelIndices) && restored;
    }
    updateChannelTooltips();
    message(juce::String(outputCount_.getNumItems() == 0
                ? "No usable output ports reported. Refresh / Retry, check device/vendor setup, or select another backend. Start is unavailable. "
                : rateChoice_.getNumItems() == 0 || buffer_.getNumItems() == 0
                ? "Rate/buffer candidates are unreported or outside engine limits; Start is unavailable. Refresh / Retry, check vendor/OS setup, or select different endpoints/backend. "
                : restored ? "Saved settings revalidated. Start remains explicit. "
                           : "Capabilities ready. Select mono/stereo driver ports. Input must be disabled or match output width. ")
            + (shared ? "Windows manages the hardware period; callback frames are application block choices. " : "")
            + (buffer_.getNumItems() == 1 ? "One buffer value reported for this configuration. " : ""));
}
void RuntimePanel::configureOffline(int channels, double rate, int frames)
{
    if (running_) return;
    ++probeSerial_; probing_ = false; scanning_ = false;
    if (backend_) { const auto result = backend_->shutdown(); juce::ignoreUnused(result); backend_.reset(); }
    inputs_.clear(); outputs_.clear(); input_.clear(juce::dontSendNotification); output_.clear(juce::dontSendNotification);
    inputPorts_.clear(); outputPorts_.clear(); endpointHint_.reset();
    offline_ = true; api_.setSelectedId(6, juce::dontSendNotification);
    for (auto* box : {&rateChoice_, &buffer_, &inputCount_, &outputCount_}) box->clear(juce::dontSendNotification);
    rateChoice_.addItem(juce::String(rate, 0), 1); rateChoice_.setSelectedId(1, juce::dontSendNotification);
    buffer_.addItem(juce::String(frames), 1); buffer_.setSelectedId(1, juce::dontSendNotification);
    inputCount_.addItem(previewChoice(0), 1); inputCount_.setSelectedId(1, juce::dontSendNotification);
    for (int i = 1; i <= 32; ++i) outputCount_.addItem(previewChoice(i), i);
    outputCount_.setSelectedId(channels, juce::dontSendNotification);
    updateChannelTooltips();
    message("Offline preview selected. Start processes silent input / script generators to visualizers only; no device opens.");
    enableControls();
}
void RuntimePanel::restoreSetup(std::optional<AudioSetupPreference> preference)
{
    if (running_) return;
    preference_ = std::move(preference);
    if (!preference_) return;
    const auto validation = validateSettings(Settings{1100, 720, true, preference_});
    if (!validation.succeeded) { message(text(validation.error)); preference_.reset(); return; }
    if (preference_->inputChannelIndices.empty())
        preference_->inputChannelIndices = legacyAudioChannels(preference_->inputChannels);
    if (preference_->outputChannelIndices.empty())
        preference_->outputChannelIndices = legacyAudioChannels(preference_->outputChannels);
    // JUCE ignores programmatic toggle changes while the button is disabled.
    advanced_.setEnabled(true);
    advanced_.setToggleState(preference_->outputChannels > 2, juce::dontSendNotification);
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
            const auto& value = *configuration.value;
            return AudioSetupPreference{static_cast<AudioBackendKind>(api_.getSelectedId() - 1),
                value.input ? inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).name : std::string{},
                outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).name,
                value.sampleRate, value.bufferFrames, value.inputChannels, value.outputChannels,
                value.inputChannelIndices, value.outputChannelIndices};
        }
    }
    return preference_;
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
AudioQuery<AudioStreamRequest> RuntimePanel::request() const
{
    if (probing_ || scanning_) return {{}, invalid("Wait for discovery/inspection to finish.")};
    AudioStreamRequest value;
    if (rateChoice_.getSelectedId() < 1 || buffer_.getSelectedId() < 1)
        return {{}, invalid("Select a reported sample rate and buffer size.")};
    value.sampleRate = rateChoice_.getText().getDoubleValue(); value.bufferFrames = buffer_.getText().getIntValue();
    value.maxFrames = std::max(512, value.bufferFrames);
    if (offline_)
    {
        value.inputChannels = inputCount_.getSelectedId() - 1;
        value.outputChannels = outputCount_.getSelectedId();
        value.inputChannelIndices = legacyAudioChannels(value.inputChannels);
        value.outputChannelIndices = legacyAudioChannels(value.outputChannels);
    }
    else
    {
        const auto inputIndex = inputCount_.getSelectedId() - 1, outputIndex = outputCount_.getSelectedId() - 1;
        if (inputIndex < 0 || outputIndex < 0 || inputIndex >= static_cast<int>(inputPorts_.size())
            || outputIndex >= static_cast<int>(outputPorts_.size()))
            return {{}, invalid("Select available driver ports. Saved channel identities may no longer exist.")};
        value.inputChannelIndices = inputPorts_[static_cast<std::size_t>(inputIndex)];
        value.outputChannelIndices = outputPorts_[static_cast<std::size_t>(outputIndex)];
        value.inputChannels = static_cast<int>(value.inputChannelIndices.size());
        value.outputChannels = static_cast<int>(value.outputChannelIndices.size());
    }
    if (value.sampleRate < 8000 || value.sampleRate > 384000 || value.bufferFrames < 1 || value.bufferFrames > 4096
        || value.outputChannels < 1 || value.outputChannels > 32 || value.inputChannels < 0
        || (value.inputChannels != 0 && value.inputChannels != value.outputChannels))
        return {{}, invalid("Start unavailable: input port width must match output port width, or input must be disabled. Mono-to-stereo routing is not supported; select a reported rate/buffer.")};
    if (!offline_)
    {
        if (!outputCaps_ || output_.getSelectedId() < 1 || !backend_) return {{}, invalid("Select native endpoints and wait for automatic inspection.")};
        value.output = outputs_.at(static_cast<std::size_t>(output_.getSelectedId() - 1)).token;
        if (value.inputChannels)
        {
            if (!inputCaps_ || input_.getSelectedId() < 2) return {{}, invalid("Select an input endpoint or disable input.")};
            value.input = inputs_.at(static_cast<std::size_t>(input_.getSelectedId() - 2)).token;
        }
    }
    return {value, {}};
}
int RuntimePanel::channels() const noexcept
{
    if (offline_) return outputCount_.getSelectedId() > 0 ? outputCount_.getSelectedId() : 2;
    const int index = outputCount_.getSelectedId() - 1;
    return index >= 0 && index < static_cast<int>(outputPorts_.size())
        ? static_cast<int>(outputPorts_[static_cast<std::size_t>(index)].size()) : 2;
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
    if (!offline_) preference_ = setupPreference();
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
        message(result.succeeded ? "Audio interrupted/device lost or changed. Stopped; reselect endpoints and restart explicitly." : text(result.error));
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
