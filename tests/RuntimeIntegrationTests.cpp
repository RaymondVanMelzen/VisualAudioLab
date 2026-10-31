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
        selectedInput = std::move(input); selectedOutput = output;
        return queryCapabilities(std::move(output), std::move(completed));
    }
    AudioQuery<AudioStreamFormat> openStream(const AudioStreamRequest& request) override
    {
        ++opens;
        return {AudioStreamFormat{request.sampleRate, request.bufferFrames, request.maxFrames, request.inputChannels, request.outputChannels}, {}};
    }
    AudioError startStream(IAudioRenderCallback& value) override { target = &value; return {}; }
    AudioError stopStream() override { target = nullptr; return {}; }
    AudioError closeStream() override { ++closes; return {}; }
    AudioQuery<AudioStreamDiagnostics> streamDiagnostics() const override { return {AudioStreamDiagnostics{target != nullptr, target != nullptr, {}, 64, {}}, {}}; }
    AudioError shutdown() override { target = nullptr; changed = {}; return {}; }
    void unplug() { ++inventory.generation; inventory.devices.clear(); if (changed) changed(inventory); }
    ChangeCallback changed;
    AudioBackendSnapshot inventory;
    IAudioRenderCallback* target{};
    int scans{}, probes{}, opens{}, closes{};
    AudioBackendKind backend_;
    bool defer{}, withInput{};
    CapabilityCallback pending;
    AudioError probeError;
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
void screenshot(juce::Component& component, juce::String name)
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("VAL_UI_REPORT_DIR", {});
    if (directory.isEmpty()) return;
    const auto file = juce::File(directory).getChildFile(name);
    auto stream = file.createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk()
            && juce::PNGImageFormat{}.writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), *stream),
            "Save actual integrated desktop screenshot.");
    stream->flush();
}
}

void runRuntimeIntegrationTests()
{
    require(supportedAudioBackends(AudioPlatform::windows) == std::vector{AudioBackendKind::asio, AudioBackendKind::wasapiExclusive, AudioBackendKind::wasapiShared}
            && supportedAudioBackends(AudioPlatform::macOS) == std::vector{AudioBackendKind::coreAudio}
            && supportedAudioBackends(AudioPlatform::other).empty(), "Backend platform policy excludes foreign APIs without fallback.");
    {
        WorkspaceService workspace;
        VisualizationSession observations;
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
        auto* api = control<juce::ComboBox>(setup, "Backend");
#if JUCE_WINDOWS
        require(api->indexOfItemId(4) < 0 && api->getNumItems() == 4, "Windows UI never offers CoreAudio.");
        const auto kind = AudioBackendKind::asio;
        const int id = 1;
#elif JUCE_MAC
        require(api->indexOfItemId(1) < 0 && api->indexOfItemId(3) < 0 && api->getNumItems() == 2, "macOS UI offers only CoreAudio and offline.");
        const auto kind = AudioBackendKind::coreAudio;
        const int id = 4;
#else
        const auto kind = AudioBackendKind::wasapiShared;
        const int id = 3;
#endif
        const AudioSetupPreference preference{kind, "Test output (not hardware)", "Test output (not hardware)", 48000, 128, 2, 2};
        auto saved = preference;
        if (kind != AudioBackendKind::asio) saved.inputName = "Test input (not hardware)";
        setup.restoreSetup(saved);
        require(creations == 0 && !setup.running() && !setup.ready(), "Restoring setup hints does not create, probe or start hardware.");
        auto* scan = control<juce::TextButton>(setup, "Scan devices");
        auto* inspect = control<juce::TextButton>(setup, "Inspect selected devices");
        auto* input = control<juce::ComboBox>(setup, "Input device");
        auto* output = control<juce::ComboBox>(setup, "Output device");
        auto* rate = control<juce::ComboBox>(setup, "Sample rate");
        auto* buffer = control<juce::ComboBox>(setup, "Buffer frames");
        auto* outCount = control<juce::ComboBox>(setup, "Output channels");
        auto* inCount = control<juce::ComboBox>(setup, "Input channels");
        auto* start = control<juce::TextButton>(setup, "Start");
        require(!buffer->isEnabled() && !start->isEnabled(), "Uninspected capabilities cannot masquerade as supported controls.");
        scan->onClick();
        require(output->getSelectedId() == 1 && input->getSelectedId() == 2 && device->probes == 0,
                "Explicit scan restores exact saved endpoints without capability probing.");
        inspect->onClick();
        require(device->probes == 1 && device->selectedInput && !device->opens
                && rate->getNumItems() == 1 && buffer->getNumItems() == 1 && inCount->getNumItems() == 3
                && setup.ready() && !setup.running(), "One combined probe restores revalidated settings, including duplex ASIO, with explicit transport.");
        if (kind == AudioBackendKind::asio)
        {
            require(input->getNumItems() == 2 && *device->selectedInput == device->selectedOutput,
                    "ASIO exposes None or the same driver, never independent input/output drivers.");
            input->setSelectedId(1, juce::sendNotificationSync);
            require(input->getSelectedId() == 1, "Explicit ASIO output-only selection is not overwritten.");
            input->setSelectedId(2, juce::sendNotificationSync);
        }
        device->defer = true;
        inspect->onClick();
        require(!rate->isEnabled() && !start->isEnabled() && setup.compactStatus().contains("inspecting"),
                "Pending probe clears old choices and disables Start.");
        device->pending({{}, {AudioErrorCode::unavailable, "Injected busy driver; retry Inspect."}});
        device->pending = {};
        require(rate->getNumItems() == 0 && !buffer->isEnabled() && inspect->isEnabled()
                && setup.status().contains("busy driver"), "Failed probe shows actionable error with retry, never stale success.");
        device->defer = false;
        device->capabilities.sampleRates.clear(); device->capabilities.bufferFrames.clear();
        inspect->onClick();
        require(!rate->isEnabled() && !buffer->isEnabled() && !setup.ready() && setup.status().contains("unreported"),
                "Unknown rate/period lists stay unknown, not fabricated defaults or host-managed claims.");
        device->capabilities.sampleRates = {48000}; device->capabilities.bufferFrames = {128};
        inspect->onClick();
        scan->onClick();
        require(output->getSelectedId() == 1 && rate->getNumItems() == 0 && !setup.ready(),
                "Refresh keeps device-name hints but invalidates capabilities until explicit inspection.");
        inspect->onClick();
        require(setup.setupPreference() == saved && setup.ready(), "Reinspection restores machine-local values without silently changing them.");
        device->inventory.devices.clear(); device->changed(device->inventory);
        require(!setup.ready() && !output->isEnabled() && setup.status().contains("No output"),
                "Empty inventories are truthful and actionable.");
        juce::ignoreUnused(id, outCount);
    }
    {
        WorkspaceService legacy;
        VisualizationSession observations;
        RuntimePanel panel(legacy, observations);
        panel.configureOffline(1);
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
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("val-ui-integration", "", false);
    require(directory.createDirectory().wasOk(), "Create isolated persistence fixture.");
    infrastructure::LocalProjectStore store(std::filesystem::path(directory.getChildFile("recent.json").getFullPathName().toWideCharPointer()));
    application::ProjectSession project(service, store);
    UiBackend* mock{};
    int creations{};
    MainComponent shell(service, &project, [&](AudioBackendKind) -> std::unique_ptr<IAudioBackend>
    {
        ++creations; auto backend = std::make_unique<UiBackend>(); mock = backend.get(); return backend;
    });
    shell.setSize(1600, 1000);
    juce::DocumentWindow window("Desktop integration validation - no hardware", juce::Colours::black, juce::DocumentWindow::allButtons);
    window.setContentNonOwned(&shell, false); window.centreWithSize(1600, 1000); window.setVisible(true);
    require(creations == 0, "Opening desktop never constructs/probes/opens a native backend.");
    auto& runtime = shell.runtime();
    runtime.configureOffline(1);
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
    require(shell.keyPressed(juce::KeyPress(juce::KeyPress::F5Key)) && runtime.running() && creations == 0,
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
            auto* api = control<juce::ComboBox>(runtime, "Backend");
            require(api, "Runtime exposes a named backend selector.");
#if JUCE_MAC
            api->setSelectedId(4, juce::sendNotificationSync);
#else
            api->setSelectedId(3, juce::sendNotificationSync);
#endif
            auto* scan = control<juce::TextButton>(runtime, "Scan devices");
            auto* inspect = control<juce::TextButton>(runtime, "Inspect selected devices");
            require(scan && inspect && mock == nullptr, "Backend switching alone does not probe drivers.");
            scan->onClick();
            require(mock && mock->scans == 1 && mock->probes == 0 && mock->opens == 0, "Scan discovers without capability probing or stream open.");
            control<juce::ComboBox>(runtime, "Output device")->setSelectedId(1, juce::sendNotificationSync);
            inspect->onClick();
            require(mock->probes == 1 && mock->opens == 0, "Inspect explicitly queries the selected device, not a fake device list.");
            require(!runtime.start().succeeded && mock->opens == 0, "Unselected sample rate/buffer/channel configuration never silently falls back or opens hardware.");
            control<juce::ComboBox>(runtime, "Sample rate")->setSelectedId(1, juce::dontSendNotification);
            control<juce::ComboBox>(runtime, "Buffer frames")->setSelectedId(1, juce::dontSendNotification);
            control<juce::ComboBox>(runtime, "Output channels")->setSelectedId(1, juce::dontSendNotification);
            scan->onClick();
            inspect->onClick();
            require(runtime.getWidth() >= 640 && control<juce::ComboBox>(runtime, "Output device")->getWidth() >= 200,
                    "Opening an initially hidden setup dock sizes controls to its actual area without requiring a window resize: runtime "
                    + std::to_string(runtime.getWidth()) + ", output control "
                    + std::to_string(control<juce::ComboBox>(runtime, "Output device")->getWidth()) + ".");
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
