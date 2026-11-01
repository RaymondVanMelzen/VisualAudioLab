#include "TestSupport.h"
#include <val/presentation/MainComponent.h>
#include <val/infrastructure/LocalProjectStore.h>
#include <val/application/ProjectSession.h>
#include <limits>

namespace
{
using namespace val;
using namespace val::application;
using namespace val::presentation;

/** Finds a real named UI control without private access or operating a native device. */
template <typename T> T* control(juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* typed = dynamic_cast<T*>(child))
        {
            if constexpr (std::is_same_v<T, juce::ComboBox>) { if (typed->getTitle() == name) return typed; }
            else if constexpr (std::is_same_v<T, juce::TextButton>) { if (typed->getButtonText() == name) return typed; }
        }
        if (auto* nested = control<T>(*child, name)) return nested;
    }
    return nullptr;
}
/** Exercises JUCE's actual button pointer path, rather than calling its onClick callback. */
void clickButton(juce::TextButton& button)
{
    const auto point = button.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
        &button, &button, now, point, now, 1, false};
    juce::Component& target = button;
    target.mouseEnter(event);
    target.mouseDown(event);
    target.mouseUp(event);
}
/** In-memory device seam; production UI uses exactly the same explicit backend contract. */
class UiBackend final : public IAudioBackend
{
public:
    explicit UiBackend(AudioBackendKind backend = AudioBackendKind::wasapiShared) : backend_(backend) {}
    AudioBackendKind kind() const noexcept override { return backend_; }
    AudioQuery<AudioBackendSnapshot> snapshot() const override { return {inventory, {}}; }
    AudioError setChangeCallback(ChangeCallback value) override { changed = std::move(value); return {}; }
    AudioError refresh() override
    {
        ++scans;
        inventory = {kind(), 1, {}, {{{kind(), 1, 1, "output"}, "Test output (not hardware)", AudioDirection::output, true}}, {}};
        if (kind() == AudioBackendKind::asio)
            inventory.devices.front().direction = AudioDirection::duplex;
        else if (withInput)
            inventory.devices.push_back({{kind(), 1, 1, "input"}, "Test input (not hardware)", AudioDirection::input, true});
        if (changed) changed(inventory);
        return {};
    }
    AudioError queryCapabilities(AudioDeviceToken, CapabilityCallback completed) override
    {
        ++probes;
        if (defer) pending = std::move(completed);
        else completed(probeError.succeeded() ? AudioQuery<AudioDeviceCapabilities>{capabilities, {}} : AudioQuery<AudioDeviceCapabilities>{{}, probeError});
        return {};
    }
    AudioError queryConfigurationCapabilities(std::optional<AudioDeviceToken> input, AudioDeviceToken output,
        CapabilityCallback completed) override
    {
        if (!requestError.succeeded()) return requestError;
        selectedInput = std::move(input); selectedOutput = output;
        return queryCapabilities(std::move(output), std::move(completed));
    }
    AudioQuery<AudioStreamFormat> openStream(const AudioStreamRequest& request) override
    {
        ++opens;
        openedRequest = request;
        return {AudioStreamFormat{request.sampleRate, request.bufferFrames, request.maxFrames, request.inputChannels, request.outputChannels}, {}};
    }
    AudioError startStream(IAudioRenderCallback& value) override { target = &value; return {}; }
    AudioError stopStream() override { target = nullptr; return {}; }
    AudioError closeStream() override { ++closes; return {}; }
    AudioQuery<AudioStreamDiagnostics> streamDiagnostics() const override { return {AudioStreamDiagnostics{target != nullptr, target != nullptr, {}, 64, {}}, {}}; }
    AudioError shutdown() override { target = nullptr; changed = {}; return {}; }
    AudioError showControlPanel(AudioDeviceToken token, ControlPanelCallback completed) override
    {
        ++panels; panelToken = std::move(token);
        completed(panelError);
        return {};
    }
    void unplug() { ++inventory.generation; inventory.devices.clear(); if (changed) changed(inventory); }
    ChangeCallback changed;
    AudioBackendSnapshot inventory;
    IAudioRenderCallback* target{};
    int scans{}, probes{}, opens{}, closes{}, panels{};
    AudioStreamRequest openedRequest;
    AudioDeviceToken panelToken;
    AudioBackendKind backend_;
    bool defer{}, withInput{};
    CapabilityCallback pending;
    AudioError probeError, requestError, panelError;
    std::optional<AudioDeviceToken> selectedInput;
    AudioDeviceToken selectedOutput;
    AudioDeviceCapabilities capabilities{{}, {"Left", "Right"}, {48000}, {128}, 128, {}, {}};
};
/** Dispatches one bounded series of GUI/worker phases, with failure propagation and no modal loop nesting. */
class Phases final : private juce::Timer
{
public:
    explicit Phases(std::vector<std::function<bool()>> phases) : phases_(std::move(phases)) {}
    void run()
    {
        startTimer(15); juce::MessageManager::getInstance()->runDispatchLoop();
        require(error_.empty(), error_); require(index_ == phases_.size(), "All integrated desktop phases completed.");
    }
private:
    void timerCallback() override
    {
        try
        {
            if (++ticks_ > 1200) throw std::runtime_error("Integrated desktop workflow exceeded 18 seconds.");
            if (index_ < phases_.size() && phases_[index_]()) ++index_;
            if (index_ != phases_.size()) return;
        }
        catch (const std::exception& error) { error_ = error.what(); }
        stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop();
    }
    std::vector<std::function<bool()>> phases_;
    std::size_t index_{};
    int ticks_{};
    std::string error_;
};
void screenshot(juce::Component& component, juce::String name, juce::Rectangle<int> area = {})
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("VAL_UI_REPORT_DIR", {});
    if (directory.isEmpty()) return;
    const auto file = juce::File(directory).getChildFile(name);
    auto stream = file.createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk()
            && juce::PNGImageFormat{}.writeImageToStream(component.createComponentSnapshot(
                area.isEmpty() ? component.getLocalBounds() : area), *stream),
            "Save actual integrated desktop screenshot.");
    stream->flush();
}
}

void runRuntimeIntegrationTests()
{
    require(supportedAudioBackends(AudioPlatform::windows) == std::vector{AudioBackendKind::asio, AudioBackendKind::wasapiShared, AudioBackendKind::wasapiExclusive, AudioBackendKind::directSound}
            && supportedAudioBackends(AudioPlatform::macOS) == std::vector{AudioBackendKind::coreAudio}
            && supportedAudioBackends(AudioPlatform::other).empty(), "Backend platform policy excludes foreign APIs without fallback.");
#if JUCE_WINDOWS
    const auto testedBackends = supportedAudioBackends(AudioPlatform::windows);
#elif JUCE_MAC
    const auto testedBackends = supportedAudioBackends(AudioPlatform::macOS);
#else
    const std::vector<AudioBackendKind> testedBackends{};
#endif
    for (const auto kind : testedBackends)
    {
        WorkspaceService workspace;
        VisualizationSession observations;
        ShellTheme theme;
        UiBackend* device{};
        int creations{};
        auto setupOwner = std::make_unique<RuntimePanel>(workspace, observations, [&](AudioBackendKind kind) -> std::unique_ptr<IAudioBackend>
        {
            ++creations;
            auto backend = std::make_unique<UiBackend>(kind); device = backend.get();
            backend->withInput = true;
            backend->capabilities.inputChannels = {"In left", "In right"};
            return backend;
        });
        auto& setup = *setupOwner;
        setup.setLookAndFeel(&theme);
        setup.setVisible(true);
        auto* api = control<juce::ComboBox>(setup, "Backend");
#if JUCE_WINDOWS
        require(api->indexOfItemId(4) < 0 && api->getNumItems() == 5 && api->indexOfItemId(5) >= 0
                && api->indexOfItemId(6) >= 0, "Windows UI offers DirectSound, not CoreAudio or fictional MME; offline has a distinct ID.");
#elif JUCE_MAC
        require(api->indexOfItemId(1) < 0 && api->indexOfItemId(3) < 0 && api->getNumItems() == 2, "macOS UI offers only CoreAudio and offline.");
#endif
        const AudioSetupPreference preference{kind, "Test output (not hardware)", "Test output (not hardware)", 48000, 128, 2, 2};
        auto saved = preference;
        if (kind != AudioBackendKind::asio) saved.inputName = "Test input (not hardware)";
        saved.inputChannelIndices = {0, 1}; saved.outputChannelIndices = {0, 1};
        require(creations == 1 && device->scans == 1 && device->probes == 0 && device->opens == 0,
                "Construction automatically scans only the selected API; unselected endpoints are not probed or opened.");
        setup.restoreSetup(saved);
        require(device->scans == 1 && device->probes == 1 && setup.ready() && !setup.running() && device->opens == 0,
                "Restoring exact device hints automatically scans and revalidates, never starts transport.");
        auto* scan = control<juce::TextButton>(setup, "Refresh / Retry");
        require(!control<juce::TextButton>(setup, "Detect rates, buffers & channels"), "Detect is removed entirely.");
        auto* input = control<juce::ComboBox>(setup, "Input device");
        auto* output = control<juce::ComboBox>(setup, "Output device");
        auto* rate = control<juce::ComboBox>(setup, "Sample rate");
        auto* buffer = control<juce::ComboBox>(setup, "Buffer frames");
        auto* outCount = control<juce::ComboBox>(setup, "Output port");
        auto* inCount = control<juce::ComboBox>(setup, "Input port");
        auto* start = control<juce::TextButton>(setup, "Start");
        auto* panel = control<juce::TextButton>(setup, "Control Panel");
        require(buffer->isEnabled() && start->isEnabled() && !panel->isEnabled(),
                "Automatically inspected candidates enable configuration; unreported driver panels remain disabled.");
        output->setSelectedId(0, juce::dontSendNotification);
        output->setSelectedId(1, juce::sendNotificationSync);
        input->setSelectedId(2, juce::sendNotificationSync);
        require(setup.ready() && device->probes >= 2 && device->opens == 0,
                "Endpoint selection automatically reloads ports; never opens an application stream.");
        juce::Label* state{};
        for (auto* child : setup.getChildren())
            if (child->getName() == "Audio setup state") state = dynamic_cast<juce::Label*>(child);
        require(state, "Persistent setup state has an accessible name.");
        for (const int width : {640, 800, 1560})
        {
            setup.setSize(width, 300);
            require(setup.getLocalBounds().contains(scan->getBounds()) && setup.getLocalBounds().contains(panel->getBounds())
                    && state->getY() < buffer->getY() && scan->getWidth() >= 100,
                    "Compact operation controls and persistent status fit the occasional setup dock.");
        }
        setup.setSize(800, 300);
        screenshot(setup, "desktop-options-" + juce::String(static_cast<int>(kind)) + "-before.png");
        screenshot(setup, "desktop-options-" + juce::String(static_cast<int>(kind)) + "-narrow.png", {0, 0, 323, 101});
        require(device->selectedInput && !device->opens && inCount->getNumItems() == 4,
                "Combined configuration probe reports the actual pair, including same-driver ASIO.");
        require(outCount->getItemText(0) == "Mono: 1: Left"
                && outCount->getText() == "Stereo: 1: Left / 2: Right"
                && inCount->getItemText(0) == "None (input disabled)"
                && inCount->getText() == "Stereo: 1: In left / 2: In right",
                "Basic choices expose single driver channels and contiguous stereo pairs with verbatim names, not first-N counts: "
                    + outCount->getItemText(0).toStdString() + "; " + outCount->getText().toStdString()
                    + "; " + inCount->getText().toStdString());
        if (kind == AudioBackendKind::asio)
        {
            require(input->getNumItems() == 2 && *device->selectedInput == device->selectedOutput,
                    "ASIO exposes None or the same driver, never independent input/output drivers.");
            input->setSelectedId(1, juce::sendNotificationSync);
            require(input->getSelectedId() == 1, "Explicit ASIO output-only selection is not overwritten.");
            input->setSelectedId(2, juce::sendNotificationSync);
        }
        device->defer = true;
        input->setSelectedId(1, juce::sendNotificationSync);
        require(!rate->isEnabled() && !start->isEnabled() && setup.compactStatus().contains("inspecting"),
                "Pending probe clears old choices and disables Start.");
        require(state->getText().contains("Loading device options") && input->isEnabled() && !scan->isEnabled(),
                "Waiting is explicit, selections remain cancellable and duplicate refresh is disabled.");
        screenshot(setup, "desktop-options-" + juce::String(static_cast<int>(kind)) + "-pending.png");
        const auto pendingProbes = device->probes;
        clickButton(*scan);
        require(device->probes == pendingProbes && device->opens == 0 && !setup.running(),
                "Disabled detection does not queue duplicate vendor calls or start audio while loading.");
        device->pending({{}, {AudioErrorCode::unavailable, "Injected busy driver; retry."}});
        device->pending = {};
        require(rate->getNumItems() == 0 && !buffer->isEnabled() && scan->isEnabled()
                && setup.status().contains("busy driver"), "Failed probe shows actionable error with retry, never stale success.");
        require(state->getText().contains("Device options failed") && state->getTooltip().contains("busy driver"),
                "Driver errors remain visible above fields with full diagnostic tooltip.");
        screenshot(setup, "desktop-options-" + juce::String(static_cast<int>(kind)) + "-failed.png");
        device->defer = false;
        device->requestError = {AudioErrorCode::unavailable, "Injected stale device: scan again."};
        clickButton(*scan);
        require(setup.status().contains("request rejected") && setup.status().contains("scan again")
                && scan->isEnabled() && !start->isEnabled() && buffer->getNumItems() == 0,
                "Immediate query rejection is distinct from a failed completion and leaves a visible retry action.");
        device->requestError = {};
        device->capabilities.sampleRates.clear(); device->capabilities.bufferFrames.clear();
        scan->onClick();
        require(!rate->isEnabled() && !buffer->isEnabled() && !setup.ready() && setup.status().contains("unreported"),
                "Unknown rate/period lists stay unknown, not fabricated defaults or host-managed claims.");
        device->capabilities.sampleRates = {48000}; device->capabilities.bufferFrames = {128};
        scan->onClick();
        require(output->getSelectedId() == 1 && rate->getNumItems() == 1 && setup.ready(),
                "Refresh keeps exact name hints and automatically revalidates capabilities.");
        require(setup.setupPreference()->inputName.empty() && setup.ready(),
                "Retry preserves the user's output-only endpoint choice instead of resurrecting saved duplex input.");
        input->setSelectedId(2, juce::sendNotificationSync);
        require(setup.setupPreference() == saved && setup.ready(),
                "Reselecting the saved input automatically restores exact revalidated settings.");
        screenshot(setup, "desktop-options-" + juce::String(static_cast<int>(kind)) + "-ready.png");
        device->capabilities.inputChannels = {"Test analog", "Test virtual A", "Test virtual B",
            "Test virtual C", "Test loopback A", "Test loopback B", ""};
        device->capabilities.outputChannels = {"Test main left", "Test main right", "Test virtual out"};
        clickButton(*scan);
        outCount->setSelectedId(1, juce::sendNotificationSync);
        inCount->setSelectedId(9, juce::sendNotificationSync);
        require(!setup.ready() && !start->isEnabled() && setup.status().contains("must match")
                && inCount->getText() == "Stereo: 1: Test analog / 2: Test virtual A",
                "Mono output and stereo input fail clearly; the engine does not invent width conversion.");
        screenshot(setup, "desktop-channel-counts-" + juce::String(static_cast<int>(kind)) + "-mismatch.png");
        outCount->setSelectedId(5, juce::sendNotificationSync);
        inCount->setSelectedId(13, juce::sendNotificationSync);
        require(setup.ready() && setup.setupPreference()->inputChannels == 2
                && setup.setupPreference()->outputChannels == 2 && device->opens == 0
                && outCount->getText() == "Stereo: 2: Test main right / 3: Test virtual out"
                && setup.setupPreference()->inputChannelIndices == std::vector<int>{4, 5}
                && setup.setupPreference()->outputChannelIndices == std::vector<int>{1, 2},
                "Later contiguous pairs preserve exact driver identities independently of bus width.");
        require(workspace.createNode({0, 0}, "Input", {{"audio.type", std::string("Input")}}).succeeded,
                "Create real input node for the selected port bus.");
        const auto inputNode = workspace.lastCreatedNode();
        require(workspace.createNode({180, 0}, "Output", {{"audio.type", std::string("Output")}}).succeeded,
                "Create real output node for the selected port bus.");
        require(workspace.connect({inputNode, domain::Port::output}, {workspace.lastCreatedNode(), domain::Port::input}).succeeded,
                "Wire the actual engine graph before explicit native Start.");
        const auto started = setup.start();
        require(started.succeeded && device->opens == 1
                && device->openedRequest.inputChannelIndices == std::vector<int>{4, 5}
                && device->openedRequest.outputChannelIndices == std::vector<int>{1, 2},
                "Only explicit Start opens a stream and forwards the exact selected ports, not first N: " + started.error);
        require(setup.stop().succeeded, "Explicit Stop quiesces the selected stream.");
        screenshot(setup, "desktop-channel-counts-" + juce::String(static_cast<int>(kind)) + "-ready.png");
        inCount->setSelectedId(1, juce::sendNotificationSync);
        require(setup.ready() && setup.setupPreference()->inputChannels == 0
                && setup.setupPreference()->inputName.empty()
                && inCount->getText() == "None (input disabled)",
                "Zero disables native capture without pretending it selects input port zero.");
        device->capabilities.hasControlPanel = true;
        scan->onClick();
        require(panel->isEnabled(), "Driver-reported Control Panel support enables its action.");
        clickButton(*panel);
        require(device->panels == 1 && device->panelToken == device->selectedOutput && !setup.running()
                && device->opens == 1, "Control Panel targets the chosen driver and revalidates without starting audio.");
        device->panelError = {AudioErrorCode::unavailable, "Injected driver panel failure"};
        clickButton(*panel);
        require(setup.status().contains("panel failure") && !setup.running() && device->opens == 1,
                "Control Panel failure remains visible without opening audio or silently changing backend.");
        device->panelError = {};
        device->defer = true;
        input->setSelectedId(1, juce::dontSendNotification);
        input->setSelectedId(2, juce::sendNotificationSync);
        auto oldCompletion = std::move(device->pending);
        input->setSelectedId(1, juce::sendNotificationSync);
        auto newCompletion = std::move(device->pending);
        oldCompletion({{}, {AudioErrorCode::unavailable, "Obsolete selection error"}});
        require(setup.compactStatus().contains("inspecting") && !setup.status().contains("Obsolete"),
                "Late selection results cannot overwrite a newer pending selection.");
        newCompletion({device->capabilities, {}});
        require(setup.ready() && !setup.running(), "Latest selection completes without automatic transport.");
        juce::ToggleButton* advanced{};
        for (auto* child : setup.getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child)) advanced = toggle;
        require(advanced && !advanced->getToggleState() && outCount->getNumItems() == 5,
                "Basic setup contains only mono ports and contiguous stereo pairs.");
        advanced->setToggleState(true, juce::dontSendNotification); advanced->onClick();
        outCount->setSelectedId(6, juce::sendNotificationSync);
        inCount->setSelectedId(1, juce::dontSendNotification);
        require(setup.ready() && setup.setupPreference()->outputChannelIndices == std::vector<int>{0, 1, 2}
                && setup.channels() == 3, "Advanced exposes genuine wider contiguous channel groups, not arbitrary mapping.");
        advanced->setToggleState(false, juce::dontSendNotification); advanced->onClick();
        require(!setup.ready() && outCount->getSelectedId() == 0,
                "Hiding an advanced selection never silently substitutes mono or stereo ports.");
        device->inventory.devices.clear(); device->changed(device->inventory);
        require(!setup.ready() && !output->isEnabled() && setup.status().contains("No output"),
                "Empty inventories are truthful and actionable.");
        setup.setLookAndFeel(nullptr);
        juce::ignoreUnused(outCount);
    }
    if (!testedBackends.empty())
    {
        WorkspaceService workspace;
        VisualizationSession observations;
        UiBackend* device{};
        auto setup = std::make_unique<RuntimePanel>(workspace, observations, [&](AudioBackendKind kind) -> std::unique_ptr<IAudioBackend>
        {
            auto backend = std::make_unique<UiBackend>(kind);
            backend->capabilities.outputChannels = {"Analog 1", "Analog 2", "Loopback 1", "Loopback 2"};
            device = backend.get();
            return backend;
        });
        auto legacy = AudioSetupPreference{testedBackends.front(), "", "Test output (not hardware)", 48000, 128, 0, 4};
        setup->restoreSetup(legacy);
        require(setup->ready() && !setup->running() && device->opens == 0
                && setup->setupPreference()->outputChannelIndices == std::vector<int>{0, 1, 2, 3},
                "Restored wider count-only hints explicitly migrate to first-N masks in Advanced, without playback: "
                    + setup->status().toStdString() + "; port: "
                    + control<juce::ComboBox>(*setup, "Output port")->getText().toStdString());
        auto missingPorts = legacy;
        missingPorts.outputChannels = 2; missingPorts.outputChannelIndices = {8, 9};
        setup->restoreSetup(missingPorts);
        require(!setup->ready() && device->opens == 0
                && control<juce::ComboBox>(*setup, "Output port")->getSelectedId() == 0,
                "Unavailable saved channel identities remain unselected, never replaced by the first two channels.");
        missingPorts.outputName = "Missing saved driver";
        setup->restoreSetup(missingPorts);
        require(!setup->ready() && device->probes == 0 && device->opens == 0
                && control<juce::ComboBox>(*setup, "Output device")->getSelectedId() == 0,
                "Missing device-name hints never select a replacement endpoint or auto-start.");
    }
    {
        WorkspaceService legacy;
        VisualizationSession observations;
        auto panelOwner = std::make_unique<RuntimePanel>(legacy, observations);
        auto& panel = *panelOwner;
        panel.configureOffline(1);
        require(control<juce::ComboBox>(panel, "Output port")->getText() == "1 channel: preview bus 1"
                && control<juce::ComboBox>(panel, "Input port")->getText() == "0 channels (input disabled)",
                "Offline channel counts refer to a preview bus, never invented hardware labels.");
        require(!legacy.createNode({0, 0}, "Invalid", {{"audio.gain", std::numeric_limits<double>::quiet_NaN()}}).succeeded
                && legacy.graph().nodes.empty() && !legacy.canUndo(), "Invalid initial metadata never creates a partial module/history edit.");
        require(legacy.createNode({0, 0}, "Source", {{"audio.type", std::string("Script")},
            {"audio.script", std::string("NODE Source AT AUDIO; OUTPUT Y: REAL; BEGIN Y := 1 END.")}}).succeeded, "Create source atomically.");
        const auto sourceNode = legacy.lastCreatedNode();
        require(legacy.createNode({180, 0}, "Legacy Gain", {{"audio.type", std::string("Gain")}, {"audio.gain", 0.5}}).succeeded, "Create legacy saved gain.");
        const auto gainNode = legacy.lastCreatedNode();
        require(legacy.createNode({360, 0}, "Output", {{"audio.type", std::string("Output")}}).succeeded, "Create output atomically.");
        const auto outputNode = legacy.lastCreatedNode();
        require(legacy.connect({sourceNode, domain::Port::output}, {gainNode, domain::Port::input}).succeeded
                && legacy.connect({gainNode, domain::Port::output}, {outputNode, domain::Port::input}).succeeded
                && panel.start().succeeded, "Start legacy graph.");
        std::array<float, 512> samples{}; float* destination[]{samples.data()};
        panel.engine().process({nullptr, destination, 0, 1, 512});
        require(std::abs(samples.back() - 0.5f) < 0.0001f, "Legacy gain processes its saved target.");
        require(legacy.setParameter(gainNode, "audio.gain", 0.25).succeeded, "Edit legacy gain target.");
        panel.synchronize(); panel.engine().process({nullptr, destination, 0, 1, 512});
        require(std::abs(samples.back() - 0.25f) < 0.0001f, "Legacy gain updates the live base layer without requiring named parameter metadata.");
        require(legacy.undo().succeeded, "Undo legacy gain target.");
        panel.synchronize(); panel.engine().process({nullptr, destination, 0, 1, 512});
        require(std::abs(samples.back() - 0.5f) < 0.0001f && panel.stop().succeeded, "Undo restores legacy gain in live processing.");
    }
    application::WorkspaceService service;
    const auto directory = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("val-ui-integration", "", false);
    require(directory.createDirectory().wasOk(), "Create isolated persistence fixture.");
    infrastructure::LocalProjectStore store(std::filesystem::path(directory.getChildFile("recent.json").getFullPathName().toWideCharPointer()));
    application::ProjectSession project(service, store);
    UiBackend* mock{};
    int creations{};
    MainComponent shell(service, &project, [&](AudioBackendKind kind) -> std::unique_ptr<IAudioBackend>
    {
        ++creations; auto backend = std::make_unique<UiBackend>(kind); mock = backend.get(); return backend;
    });
    shell.setSize(1600, 1000);
    juce::DocumentWindow window("Desktop integration validation - no hardware", juce::Colours::black, juce::DocumentWindow::allButtons);
    window.setContentNonOwned(&shell, false); window.centreWithSize(1600, 1000); window.setVisible(true);
    require(creations == 1 && mock->scans == 1 && mock->probes == 0 && mock->opens == 0,
            "Opening desktop automatically enumerates the selected API, without probing unselected endpoints or opening audio.");
    auto& runtime = shell.runtime();
    runtime.configureOffline(1);
    mock = nullptr;
    shell.insertModule("Script"); const auto script = service.lastCreatedNode();
    shell.insertModule("Gain"); const auto gain = service.lastCreatedNode();
    shell.insertModule("Oscilloscope"); const auto analyzer = service.lastCreatedNode();
    shell.insertModule("Output"); const auto output = service.lastCreatedNode();
    require(service.moveNodes({{script, {30, 80}}, {gain, {260, 80}}, {analyzer, {490, 80}}, {output, {720, 80}}}).succeeded, "Arrange executable patch.");
    const std::string source = "NODE Preview AT AUDIO; OUTPUT Y: REAL; PARAM Level: REAL = 0.25 RANGE 0 .. 1; BEGIN Y := Level END.";
    require(service.setParameter(script, "audio.script", source).succeeded
            && service.setParameter(gain, "audio.param.gain", 0.5).succeeded, "Persist authored Script and FX targets.");
    require(service.connect({script, domain::Port::output}, {gain, domain::Port::input}).succeeded
            && service.connect({gain, domain::Port::output}, {analyzer, domain::Port::input}).succeeded
            && service.connect({analyzer, domain::Port::output}, {output, domain::Port::input}).succeeded, "Wire Script -> FX -> Analyzer -> Output with real edit commands.");
    shell.dockWorkspace().canvas().synchronise();
    shell.dockWorkspace().canvas().fitGraph();
    shell.dockWorkspace().signalPreview().setTarget(analyzer);
    shell.dockWorkspace().showPanel(PanelId::propertyInspector);
    screenshot(shell, "desktop-review-workspace.png");
    shell.dockWorkspace().showPanel(PanelId::audioSettings);
    const auto path = std::filesystem::path(directory.getChildFile("script-fx.valab").getFullPathName().toWideCharPointer());
    require(shell.saveProjectTo(path).succeeded, "Integrated executable source/FX/analysis metadata saves through File workflow.");
    require(shell.keyPressed(juce::KeyPress(juce::KeyPress::F5Key)) && runtime.running() && creations == 1,
            "Explicit desktop F5 command starts real offline engine with zero device interaction.");
    std::uint64_t initialRevision{};
    std::uint64_t beforeScheduled{};
    int observedTicks{};
    Phases phases({
        [&]
        {
            if (++observedTicks < 12 || shell.dockWorkspace().visualizations().diagnostics().jobs == 0) return false;
            const auto stats = runtime.engine().telemetry();
            require(stats.value && stats.value->frames > 0 && stats.value->running, "UI transport drives actual offline audio processing.");
            initialRevision = stats.value->activeRevision;
            std::array<float, 32> samples{}; float* destination[]{samples.data()};
            runtime.engine().process({nullptr, destination, 0, 1, 32});
            require(std::abs(samples[0] - 0.125f) < 0.0001f, "Authored script and effect execute expected samples, not a fake preview.");
            shell.dockWorkspace().canvas().selectRectangle({490, 80, 140, 80}, false, false);
            shell.dockWorkspace().showSignalInspector();
            return true;
        },
        [&]
        {
            auto& preview = shell.dockWorkspace().signalPreview();
            if (runtime.engine().telemetry().value->samplePosition < 28000
                || !preview.plot(VisualizationKind::oscilloscope).getDescription().contains("Live")) return false;
            require(preview.target() == analyzer && preview.demanding(), "Canvas selection receives live analysis in the visible inspector.");
            screenshot(shell, "desktop-integration-running.png");
            auto& desktop = juce::Desktop::getInstance();
            bool monitorCaptured{};
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* monitor = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i));
                    monitor && monitor->getName() == "Signal Inspector")
                {
                    auto* view = dynamic_cast<VisualizationWidgets*>(monitor->getContentComponent());
                    require(view && view->target() == analyzer && view->pinned(), "Persistent signal monitor shares the real selected source with independent pin state.");
                    if (!view->plot(VisualizationKind::rms).getDescription().contains("Live")) return false;
                    screenshot(*view, "desktop-integration-signal-inspector.png");
                    monitorCaptured = true;
                }
            require(monitorCaptured, "Actual non-modal signal inspector is visible.");
            shell.dockWorkspace().canvas().selectRectangle({30, 80, 140, 80}, false, false);
            for (auto* child : shell.dockWorkspace().panel(PanelId::propertyInspector).getChildren())
                if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(child)) tabs->setCurrentTabIndex(0);
            screenshot(shell, "desktop-integration-script.png");
            require(service.setParameter(gain, "audio.param.gain", 0.25).succeeded, "Edit saved FX target while running.");
            runtime.synchronize();
            return true;
        },
        [&]
        {
            const auto stats = runtime.engine().telemetry();
            if (stats.value->samplePosition < 12000) return false;
            require(stats.value->activeRevision == initialRevision, "Parameter edits preserve prepared state/smoothing instead of replacing graph.");
            require(service.undo().succeeded, "Undo persisted parameter target."); runtime.synchronize();
            require(service.createNode({900, 200}).succeeded, "Legacy generic structural nodes remain editable.");
            runtime.synchronize();
            require(runtime.running() && runtime.status().contains("last accepted audio"), "Non-executable structural content is diagnosed while last valid audio remains active.");
            require(service.undo().succeeded, "Undo generic structural insertion."); runtime.synchronize();
            require(service.setParameter(script, "audio.script",
                std::string("NODE Edited AT AUDIO; OUTPUT Y: REAL; BEGIN Y := 0.125 END.")).succeeded,
                "Edit and publish script source through the persisted parameter boundary.");
            runtime.synchronize();
            return true;
        },
        [&]
        {
            const auto stats = runtime.engine().telemetry();
            if (stats.value->activeRevision <= initialRevision) return false;
            std::array<float, 32> samples{}; float* destination[]{samples.data()};
            runtime.engine().process({nullptr, destination, 0, 1, 32});
            require(std::abs(samples[0] - 0.0625f) < 0.0001f, "Live script revision executes new generated DSP through existing plan handoff.");
            runtime.schedule(gain, 0, 0.75f, 0.1);
            beforeScheduled = runtime.engine().telemetry().value->appliedEvents;
            return true;
        },
        [&]
        {
            const auto stats = runtime.engine().telemetry();
            if (stats.value->appliedEvents <= beforeScheduled) return false;
            require(stats.value->missedEvents == 0, "UI scheduling maps seconds ahead to the correct epoch/sample frame.");
            require(service.setParameter(script, "audio.script",
                std::string("NODE Fault AT AUDIO; OUTPUT Y: REAL; PARAM Divisor: REAL = 0 RANGE 0 .. 1; BEGIN Y := 1 / Divisor END.")).succeeded,
                "Publish a valid bounded kernel with a runtime arithmetic fault.");
            runtime.synchronize();
            return true;
        },
        [&]
        {
            if (runtime.running()) return false;
            require(runtime.status().contains("Script node") && runtime.status().contains("fault")
                    && runtime.status().contains("Transport stopped"), "Latched script fault reaches source-aware desktop diagnostics and safely stops transport.");
            require(shell.loadProjectFrom(path).succeeded && !runtime.running(), "Project load quiesces audio before replacement; never automatically restarts.");
            require(std::get<std::string>(service.graph().nodes.at(script).parameters.at("audio.script")) == source
                    && std::get<double>(service.graph().nodes.at(gain).parameters.at("audio.param.gain")) == 0.5,
                    "Source, module identities, visualization kind and effect targets survive actual JSON round trip.");
            return true;
        },
        [&]
        {
            shell.dockWorkspace().showPanel(PanelId::audioSettings);
            auto* api = control<juce::ComboBox>(runtime, "Backend");
            require(api, "Runtime exposes a named backend selector.");
#if JUCE_MAC
            api->setSelectedId(4, juce::sendNotificationSync);
#else
            api->setSelectedId(3, juce::sendNotificationSync);
#endif
            auto* scan = control<juce::TextButton>(runtime, "Refresh / Retry");
            require(scan && mock, "Backend switching automatically creates and scans the chosen API.");
            require(mock && mock->scans == 1 && mock->probes == 0 && mock->opens == 0, "Scan discovers without capability probing or stream open.");
            control<juce::ComboBox>(runtime, "Output device")->setSelectedId(1, juce::sendNotificationSync);
            require(mock->probes == 1 && mock->opens == 0, "Endpoint selection automatically inspects, never opens.");
            control<juce::ComboBox>(runtime, "Sample rate")->setSelectedId(0, juce::dontSendNotification);
            require(!runtime.start().succeeded && mock->opens == 0, "Unselected sample rate/buffer/channel configuration never silently falls back or opens hardware.");
            control<juce::ComboBox>(runtime, "Sample rate")->setSelectedId(1, juce::dontSendNotification);
            control<juce::ComboBox>(runtime, "Buffer frames")->setSelectedId(1, juce::dontSendNotification);
            control<juce::ComboBox>(runtime, "Output port")->setSelectedId(1, juce::dontSendNotification);
            clickButton(*scan);
            require(runtime.getWidth() >= 640 && control<juce::ComboBox>(runtime, "Output device")->getWidth() >= 200,
                    "Opening an initially hidden setup dock sizes controls to its actual area without requiring a window resize: runtime "
                    + std::to_string(runtime.getWidth()) + ", output control "
                    + std::to_string(control<juce::ComboBox>(runtime, "Output device")->getWidth()) + ".");
            auto* viewport = runtime.findParentComponentOfClass<juce::Viewport>();
            require(viewport && runtime.isShowing() && scan->isShowing()
                    && viewport->getViewArea().contains(scan->getBounds())
                    && viewport->getViewArea().contains(juce::Rectangle<int>(8, 60, 276, 20)),
                    "Opening the real setup dock reveals operation controls and status without resizing or horizontal scrolling: "
                    + (viewport ? viewport->getViewArea().toString().toStdString() : "no viewport")
                    + "; showing: " + std::to_string(runtime.isShowing()));
            require(scan->getAccessibilityHandler()
                    && scan->getAccessibilityHandler()->getTitle() == "Refresh / Retry",
                    "The visible peer exposes the optional retry action to accessibility.");
            screenshot(*viewport, "desktop-options-real-dock.png");
            screenshot(runtime, "desktop-integration-device-settings.png");
            screenshot(shell, "desktop-review-audio-setup.png");
            return true;
        },
        [&]
        {
            require(runtime.ready() && control<juce::ComboBox>(runtime, "Buffer frames")->getNumItems() == 1,
                    "Refresh/restore/inspection does not queue endpoint onChange callbacks that later erase capabilities.");
            require(runtime.start().succeeded && mock->opens == 1 && mock->target, "Explicit native Start negotiates selected settings and attaches real engine through injected device seam.");
            control<juce::TextButton>(runtime, "Done")->onClick();
            require(runtime.running() && !shell.dockWorkspace().layout().placement(PanelId::audioSettings).visible,
                    "Done hides occasional setup without changing transport or discarding configuration.");
            std::array<float, 128> samples{}; float* destination[]{samples.data()};
            mock->target->process({nullptr, destination, 0, 1, 128});
            require(std::abs(samples[0] - 0.125f) < 0.0001f, "Native render contract consumes the persisted executable graph.");
            mock->unplug();
            require(!runtime.running() && !mock->target && mock->closes > 0, "Hot-plug invalidation stops/quiesces native stream and requires explicit reselection.");
            return true;
        }
    });
    phases.run();
    window.setVisible(false); window.clearContentComponent();
    require(directory.getChildFile("script-fx.valab").deleteFile() && directory.getChildFile("recent.json").deleteFile(), "Clean exact test persistence files.");
    require(directory.deleteFile(), "Remove now-empty fixture directory.");
}
