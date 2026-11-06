#include <val/presentation/MainComponent.h>
#include <val/presentation/BuildInfo.h>
#include <val/presentation/Typography.h>
#include <val/presentation/WindowPlacement.h>
#include <ValResources.h>
#include <cctype>

namespace val::presentation
{
namespace
{
class AboutContent final : public juce::Component, private juce::KeyListener
{
public:
    explicit AboutContent(std::function<void()> dismiss) : dismiss_(std::move(dismiss))
    {
        setName("About Visual Audio Lab");
        setWantsKeyboardFocus(true);
        logo_.setName("Visual Audio Lab application logo");
        logo_.setImage(juce::ImageCache::getFromMemory(val_resources::VAL_256x256_png,
                                                       val_resources::VAL_256x256_pngSize));
        logo_.setImagePlacement(juce::RectanglePlacement::centred
                                | juce::RectanglePlacement::onlyReduceInSize);
        addAndMakeVisible(logo_);
        product_.setName("About product name");
        product_.setText("Visual Audio Lab", juce::dontSendNotification);
        product_.setFont(Typography::ui(24, true));
        product_.setColour(juce::Label::textColourId, ShellTheme::textPrimary());
        product_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(product_);
        version_.setName("About product version");
        version_.setText("Version " + juce::String(productVersion), juce::dontSendNotification);
        version_.setFont(Typography::numeric(18));
        version_.setColour(juce::Label::textColourId, ShellTheme::textPrimary());
        version_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(version_);
        revision_.setName("About build revision");
        revision_.setText("Build " + juce::String(buildRevision), juce::dontSendNotification);
        revision_.setFont(Typography::font(TextRole::status));
        revision_.setColour(juce::Label::textColourId, ShellTheme::textSecondary());
        revision_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(revision_);
        close_.setButtonText("Close");
        close_.setTitle("Close About dialog");
        close_.setTooltip("Close this dialog and return to Visual Audio Lab.");
        close_.onClick = [this] { dismiss_(); };
        close_.addKeyListener(this);
        addAndMakeVisible(close_);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(ShellTheme::background());
        graphics.setColour(ShellTheme::border());
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(22);
        auto footer = bounds.removeFromBottom(42);
        close_.setBounds(footer.removeFromRight(112).reduced(2, 4));
        if (getWidth() < 460 || getHeight() < 270)
        {
            auto main = bounds.reduced(0, 6);
            const auto logoSize = juce::jmin(96, juce::jmax(64, main.getHeight() / 2));
            logo_.setBounds(main.removeFromTop(logoSize).withSizeKeepingCentre(logoSize, logoSize));
            product_.setBounds(main.removeFromTop(36));
            version_.setBounds(main.removeFromTop(30));
            revision_.setBounds(main.removeFromTop(26));
            return;
        }
        auto main = bounds.reduced(0, 10);
        const auto logoWidth = juce::jmin(190, main.getHeight());
        logo_.setBounds(main.removeFromLeft(logoWidth).withSizeKeepingCentre(logoWidth, logoWidth));
        auto information = main.reduced(24, 0);
        product_.setBounds(information.removeFromTop(48));
        version_.setBounds(information.removeFromTop(36));
        revision_.setBounds(information.removeFromTop(30));
    }

    void focusCloseButton() { close_.grabKeyboardFocus(); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            dismiss_();
            return true;
        }
        return false;
    }

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        return keyPressed(key);
    }

    std::function<void()> dismiss_;
    juce::ImageComponent logo_;
    juce::Label product_, version_, revision_;
    juce::TextButton close_;
};
}

MainComponent::MainComponent(application::WorkspaceService& workspace, application::ProjectSession* project, RuntimePanel::BackendFactory backends)
    : workspace_(workspace), project_(project)
{
    setLookAndFeel(&theme_);
    setName("Visual Audio Lab application shell");
    setWantsKeyboardFocus(true);
    if (!Typography::bundledFontsAvailable() || !Typography::tabularFiguresAvailable())
        juce::Logger::writeToLog("Typography: bundled font or tabular feature unavailable; using explicit platform fallback. Numeric fields retain fixed bounds.");
    banner_.setText("Explicit Start routes audio. Offline preview uses no device. Invalid edits keep last accepted audio; Stop is always available.",
                   juce::dontSendNotification);
    banner_.setColour(juce::Label::textColourId, ShellTheme::textSecondary());
    banner_.setTooltip("Choose modules, connect a valid graph, configure Audio Setup from the Audio menu, then Start explicitly.");

    runtime_ = std::make_unique<RuntimePanel>(workspace_, dockWorkspace_.visualizations(), std::move(backends));
    runtime_->onStatus = [this](juce::String value)
    {
        banner_.setText(runtime_->compactStatus() + " | Audio menu: Audio Setup; Start / Stop are explicit.",
                        juce::dontSendNotification);
        banner_.setTooltip(value);
        showStatus(value); commands_.commandStatusChanged();
        updateTransportControls();
    };
    runtime_->onStarted = [this] { dockWorkspace_.canvas().synchronise(); };
    runtime_->onDismissSetup = [this]
    {
        if (audioSetupWindow_)
            audioSetupWindow_->setVisible(false);
    };
    runtime_->setSize(720, 440);
    catalog_ = std::make_unique<ModuleCatalog>();
    catalog_->onInsert = [this](std::string type) { insertModule(std::move(type)); };
    dockWorkspace_.setPanelContent(PanelId::moduleBrowser, *catalog_);
    editor_ = std::make_unique<ModuleEditor>(workspace_);
    editor_->onChanged = [this] { refresh(); };
    editor_->onRuntimeParameter = [this](auto node, auto index, auto value, auto action) { runtime_->parameter(node, index, value, action); };
    editor_->onScheduleParameter = [this](auto node, auto index, auto value, auto seconds) { runtime_->schedule(node, index, value, seconds); };
    editor_->onReadGeneratorMeters = [this](auto node)
    {
        return runtime_->running() ? dockWorkspace_.visualizations().generatorMeters(node)
                                   : GeneratorMeterReadout{juce::String::fromUTF8("Stopped · Start transport to measure")};
    };
    inspectorTabs_.addTab("Properties / Script", ShellTheme::surface(), editor_.get(), false);
    inspectorTabs_.addTab("Signal", ShellTheme::surface(), &dockWorkspace_.signalPreview(), false);
    dockWorkspace_.setPanelContent(PanelId::propertyInspector, inspectorTabs_);
    dockWorkspace_.canvas().onObservationTarget = [this](auto node)
    {
        editor_->setNode(node);
        dockWorkspace_.signalPreview().followSelection(node);
        updateGeneratorDisplay();
        dockWorkspace_.visualizations().setEditorTarget(0);
        const auto found = workspace_.graph().nodes.find(node);
        if (found != workspace_.graph().nodes.end())
        {
            const auto typeField = found->second.parameters.find("audio.type");
            const auto* type = typeField == found->second.parameters.end()
                ? nullptr : std::get_if<std::string>(&typeField->second);
            if (type && type->find("Generator") != std::string::npos)
            {
                dockWorkspace_.visualizations().setEditorTarget(node);
                dockWorkspace_.signalPreview().showKind(VisualizationKind::oscilloscope);
                inspectorTabs_.setCurrentTabIndex(0);
            }
            else dockWorkspace_.visualizations().setEditorTarget(0);
            if (const auto kind = found->second.parameters.find("visualization.kind"); kind != found->second.parameters.end())
                if (const auto* name = std::get_if<std::string>(&kind->second))
                {
                    const auto display = *name == "Spectrum Analyzer" ? VisualizationKind::spectrum
                        : *name == "Peak Meter" ? VisualizationKind::peak
                        : *name == "RMS Meter" ? VisualizationKind::rms
                        : *name == "Correlation Meter" ? VisualizationKind::correlation
                        : *name == "Guitar Tuner" ? VisualizationKind::guitarTuner : VisualizationKind::oscilloscope;
                    dockWorkspace_.signalPreview().showKind(display);
                    inspectorTabs_.setCurrentTabIndex(1);
                }
        }
    };
    dockWorkspace_.canvas().onModuleDropped = [this](std::string type, juce::Point<float> position)
    {
        insertModule(std::move(type), position);
    };
    dockWorkspace_.canvas().onModuleDropRejected = [this](juce::String message)
    {
        showStatus(message);
    };
    dockWorkspace_.canvas().endpointChannels = [this](domain::EndpointRole role)
    {
        return runtime_->endpointChannelNames(role);
    };
    dockWorkspace_.canvas().selectedEndpointChannels = [this](domain::GraphId endpoint)
    {
        return runtime_->selectedEndpointChannelIndices(endpoint);
    };
    dockWorkspace_.canvas().endpointStatus = [this](domain::GraphId endpoint)
    {
        return runtime_->endpointReadiness(endpoint);
    };
    dockWorkspace_.canvas().assignEndpointChannels = [this](domain::GraphId endpoint, const std::vector<int>& channels)
    {
        return runtime_->assignEndpointChannels(endpoint, channels);
    };
    for (auto* component : std::initializer_list<juce::Component*>{
             &menu_, &toolbar_, &banner_, &status_, &dockWorkspace_, &patches_, &addPatch_, &startAudio_, &stopAudio_})
        addAndMakeVisible(component);
    patches_.setName("Active patch");
    patches_.setTooltip("Switch patches without losing their graph or undo history.");
    patches_.onChange = [this]
    {
        const auto index = patches_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(workspace_.project().patches.size()))
            return;
        captureProjectView();
        const auto stopped = runtime_->stop();
        if (!stopped.succeeded) { showStatus(juce::String::fromUTF8(stopped.error.c_str())); return; }
        auto patch = workspace_.project().patches.begin();
        std::advance(patch, index);
        report(workspace_.selectPatch(patch->first));
    };
    addPatch_.onClick = [this]
    {
        captureProjectView();
        report(workspace_.addPatch("Patch " + std::to_string(workspace_.project().patches.size() + 1)));
    };

    commands_.registerAllCommandsForTarget(this);
    commands_.setFirstCommandTarget(this);
    stopAudio_.setCommandToTrigger(&commands_, stopTransport, true);
    startAudio_.onClick = [this] { toggleTransportAction(); };
    addKeyListener(commands_.getKeyMappings());
    dockWorkspace_.onLayoutChanged = [this]
    {
        commands_.commandStatusChanged();
        menuItemsChanged();
        showStatus("Layout changed. Save Project to retain it.");
    };
    dockWorkspace_.canvas().onEdit = [this](const application::Result& result)
    {
        showStatus(result.succeeded ? "Graph edit accepted."
                                    : juce::String::fromUTF8(result.error.c_str()));
        commands_.commandStatusChanged();
        menuItemsChanged();
        if (displayedPatch_ != workspace_.project().activePatch)
            refresh();
        else
            updateProjectChrome();
        runtime_->synchronize();
    };
    banner_.setText(runtime_->compactStatus() + " | Audio menu: Audio Setup; Start / Stop are explicit.", juce::dontSendNotification);
    showStatus("Ready. Modules are reusable audio functions; placed nodes are instances. Configure from Audio > Audio Setup, then Start explicitly.");
    updateTransportControls();
    refresh();
    if (project_ && !project_->warning().empty())
        showStatus(juce::String::fromUTF8(project_->warning().c_str()));
}

MainComponent::~MainComponent()
{
    aboutWindow_.reset();
    audioSetupWindow_.reset();
    dockWorkspace_.canvas().onObservationTarget = nullptr;
    dockWorkspace_.canvas().onModuleDropped = nullptr;
    dockWorkspace_.canvas().onModuleDropRejected = nullptr;
    dockWorkspace_.canvas().endpointChannels = nullptr;
    dockWorkspace_.canvas().selectedEndpointChannels = nullptr;
    dockWorkspace_.canvas().endpointStatus = nullptr;
    dockWorkspace_.canvas().assignEndpointChannels = nullptr;
    inspectorTabs_.clearTabs();
    removeKeyListener(commands_.getKeyMappings());
    menu_.setModel(nullptr);
    dockWorkspace_.onLayoutChanged = nullptr;
    dockWorkspace_.canvas().onEdit = nullptr;
    setLookAndFeel(nullptr);
}

MainComponent::AudioSetupWindow::AudioSetupWindow(RuntimePanel& runtime)
    : juce::DocumentWindow("Audio Setup", ShellTheme::background(), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&runtime, false);
    setResizable(true, true);
    setResizeLimits(640, 380, 1100, 900);
    centreWithSize(720, 440);
}

MainComponent::AboutWindow::AboutWindow(MainComponent& owner)
    : juce::DocumentWindow("About Visual Audio Lab", ShellTheme::background(), closeButton),
      owner_(&owner)
{
    setUsingNativeTitleBar(true);
    auto safe = juce::Component::SafePointer<AboutWindow>(this);
    setContentOwned(new AboutContent([safe]
    {
        if (safe)
            safe->dismiss();
    }), true);
    setResizable(false, false);
    fitToOwnerDisplay();
}

void MainComponent::AboutWindow::closeButtonPressed()
{
    setVisible(false);
    if (owner_)
        owner_->grabKeyboardFocus();
}

void MainComponent::AboutWindow::dismiss()
{
    closeButtonPressed();
}

void MainComponent::AboutWindow::fitToOwnerDisplay()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = owner_ ? displays.getDisplayForRect(owner_->getScreenBounds()) : nullptr;
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display == nullptr)
    {
        centreWithSize(560, 320);
        return;
    }

    const auto area = display->userArea;
    const auto width = juce::jmin(560, area.getWidth());
    const auto height = juce::jmin(320, area.getHeight());
    setBounds(juce::Rectangle<int>(0, 0, width, height).withCentre(area.getCentre()));
}

void MainComponent::showAudioSetup()
{
    if (!audioSetupWindow_)
    {
        audioSetupWindow_ = std::make_unique<AudioSetupWindow>(*runtime_);
        audioSetupWindow_->setLookAndFeel(&theme_);
    }
    audioSetupWindow_->setVisible(true);
    audioSetupWindow_->toFront(true);
}

void MainComponent::showAbout()
{
    if (!aboutWindow_)
    {
        aboutWindow_ = std::make_unique<AboutWindow>(*this);
        aboutWindow_->setLookAndFeel(&theme_);
    }
    aboutWindow_->fitToOwnerDisplay();
    aboutWindow_->setVisible(true);
    aboutWindow_->toFront(true);
    if (auto* content = dynamic_cast<AboutContent*>(aboutWindow_->getContentComponent()))
        content->focusCloseButton();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::background());
}
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() != juce::KeyPress::F5Key) return false;
    if (key.getModifiers().isShiftDown())
        stopTransportAction();
    else
        toggleTransportAction();
    return true;
}
void MainComponent::toggleTransportAction()
{
    if (transportTransitionPending_ || startFailureDialogPending_) return;
    if (runtime_->running())
    {
        stopTransportAction();
        return;
    }

    transportTransitionPending_ = true;
    updateTransportControls();
    const auto result = runtime_->start();
    transportTransitionPending_ = false;
    if (!result.succeeded) showStartFailure(result.error);
    updateTransportControls();
}

void MainComponent::stopTransportAction()
{
    if (transportTransitionPending_ || !runtime_->running()) return;
    transportTransitionPending_ = true;
    updateTransportControls();
    const auto result = runtime_->stop();
    transportTransitionPending_ = false;
    if (!result.succeeded) showStatus(juce::String::fromUTF8(result.error.c_str()));
    updateTransportControls();
}

juce::String MainComponent::startFailureGuidance(const std::string& detail) const
{
    auto normalized = detail;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (normalized.find("select a native output device") != std::string::npos
        || normalized.find("select output explicitly") != std::string::npos)
        return "Choose an output device in Audio > Audio Setup, then assign its named channel in the Outputs lane.";
    if (normalized.find("assign at least one named channel") != std::string::npos)
        return "Assign at least one available channel to an enabled output row on the right side of the canvas.";
    if (normalized.find("input rows are assigned") != std::string::npos)
        return "Choose and inspect the input device in Audio > Audio Setup, or unassign the input row.";
    if (normalized.find("patch is empty") != std::string::npos)
        return "Add a generator or processor, connect a source to an output row, then try Start again.";
    if (normalized.find("cycle") != std::string::npos)
        return "Remove the cycle shown in the patch. Audio routes must flow from sources to outputs.";
    if (normalized.find("channel") != std::string::npos || normalized.find("layout") != std::string::npos)
        return "Review the named channel assignments and cable layouts shown in the canvas, then correct the highlighted route.";
    if (normalized.find("device") != std::string::npos || normalized.find("driver") != std::string::npos
        || normalized.find("stream") != std::string::npos || normalized.find("open") != std::string::npos)
        return "Check that the selected audio device is available and not in use, then retry Start from the chosen configuration.";
    return "Review the patch and Audio Setup. Correct the reported issue before trying Start again.";
}

void MainComponent::showStartFailure(const std::string& detail)
{
    if (startFailureDialogPending_) return;
    startFailureDialogPending_ = true;
    const auto guidance = startFailureGuidance(detail);
    showStatus("Audio could not start. " + guidance);
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Audio engine could not start",
        "The engine is stopped; no device fallback was selected.\n\n"
        + guidance + "\n\nTechnical details: " + juce::String::fromUTF8(detail.c_str()),
        "OK", getTopLevelComponent(), juce::ModalCallbackFunction::create([safe](int)
        {
            if (!safe) return;
            safe->startFailureDialogPending_ = false;
            safe->updateTransportControls();
        }));
    updateTransportControls();
}

void MainComponent::updateTransportControls()
{
    const auto running = runtime_ && runtime_->running();
    startAudio_.setButtonText(running ? "Stop (F5)" : "Start (F5)");
    startAudio_.setEnabled(!transportTransitionPending_ && !startFailureDialogPending_);
    stopAudio_.setEnabled(running && !transportTransitionPending_);
    startAudio_.setTitle(running ? "Stop audio engine" : "Start audio engine");
    startAudio_.setTooltip(running ? "Stop the audio engine (F5)." : "Start the audio engine (F5).");
    commands_.commandStatusChanged();
    menuItemsChanged();
}
void MainComponent::insertModule(std::string type, std::optional<juce::Point<float>> placement)
{
    if (type == "Input" || type == "Output")
    {
        showStatus("Hardware inputs and outputs are managed as fixed rows in the canvas lanes, not inserted as modules.");
        return;
    }
    const auto screenPosition = placement.value_or(dockWorkspace_.canvas().viewport().getCentre().toFloat());
    const auto position = dockWorkspace_.canvas().toPatch(screenPosition);
    std::map<std::string, domain::Parameter> values;
    std::string name = type;
    if (type == "Oscilloscope" || type == "Spectrum Analyzer" || type == "Peak Meter"
        || type == "RMS Meter" || type == "Correlation Meter" || type == "Guitar Tuner")
    {
        values["visualization.kind"] = type;
        type = "Analyzer";
    }
    values["audio.type"] = type;
    if (type == "Signal Generator")
        values["audio.param.levelDb"] = -6.0;
    else if (type == "Noise Generator")
        values["audio.param.levelDbRms"] = -6.0;
    if (type == "Script")
    {
        const auto count = runtime_->channels();
        std::string source = "NODE ScriptPass AT AUDIO;\n";
        for (int i = 1; i <= count; ++i) source += "INPUT In" + std::to_string(i) + ": REAL;\n";
        for (int i = 1; i <= count; ++i) source += "OUTPUT Out" + std::to_string(i) + ": REAL;\n";
        source += "BEGIN\n";
        for (int i = 1; i <= count; ++i) source += " Out" + std::to_string(i) + " := In" + std::to_string(i) + (i == count ? "\n" : ";\n");
        source += "END.";
        values["audio.script"] = std::move(source);
    }
    const auto result = workspace_.createNode({position.x, position.y}, name, std::move(values));
    report(result);
    if (result.succeeded)
    {
        editor_->setNode(workspace_.lastCreatedNode());
        dockWorkspace_.showPanel(PanelId::propertyInspector);
        inspectorTabs_.setCurrentTabIndex(0);
    }
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    menu_.setBounds(bounds.removeFromTop(26));
    const bool compact = bounds.getWidth() < 610;
    toolbar_.setBounds(bounds.removeFromTop(compact ? 66 : 40));
    auto info = bounds.removeFromTop(compact ? 64 : 38);
    if (compact)
    {
        auto transport = info.removeFromTop(30);
        startAudio_.setBounds(transport.removeFromLeft(90).reduced(2));
        stopAudio_.setBounds(transport.removeFromLeft(60).reduced(2));
        banner_.setBounds(transport.reduced(2, 0));
        auto patchControls = info;
        addPatch_.setBounds(patchControls.removeFromRight(95).reduced(2));
        patches_.setBounds(patchControls.removeFromRight(170).reduced(2));
    }
    else
    {
        startAudio_.setBounds(info.removeFromLeft(90).reduced(2, 4));
        stopAudio_.setBounds(info.removeFromLeft(60).reduced(2, 4));
        addPatch_.setBounds(info.removeFromRight(95).reduced(2, 4));
        patches_.setBounds(info.removeFromRight(170).reduced(2, 4));
        banner_.setBounds(info);
    }
    status_.setBounds(bounds.removeFromBottom(34));
    dockWorkspace_.setBounds(bounds.reduced(0, 4));
}

void MainComponent::showStatus(const juce::String& text)
{
    status_.setMessage(text);
}

bool MainComponent::inspectorVisible() const noexcept
{
    return dockWorkspace_.layout().placement(PanelId::propertyInspector).visible;
}

void MainComponent::setInspectorVisible(bool visible)
{
    dockWorkspace_.setPanelVisible(PanelId::propertyInspector, visible);
}

void MainComponent::refresh()
{
    toolbar_.setWorkspaceName(juce::String::fromUTF8(workspace_.workspace().name.c_str()));
    dockWorkspace_.canvas().synchronise();
    if (displayedPatch_ != workspace_.project().activePatch)
    {
        if (runtime_) { const auto stopped = runtime_->stop(); if (!stopped.succeeded) showStatus(juce::String::fromUTF8(stopped.error.c_str())); }
        displayedPatch_ = workspace_.project().activePatch;
        dockWorkspace_.visualizations().reset();
        dockWorkspace_.canvas().resetForProject();
    }
    updateProjectChrome();
    commands_.commandStatusChanged();
    if (runtime_) runtime_->synchronize();
    if (editor_) editor_->refresh();
    updateGeneratorDisplay();
}

void MainComponent::updateGeneratorDisplay()
{
    GeneratorDisplayInfo info;
    const auto id = dockWorkspace_.signalPreview().target();
    const auto found = workspace_.graph().nodes.find(id);
    if (found == workspace_.graph().nodes.end())
    {
        dockWorkspace_.signalPreview().setGeneratorContext(std::move(info));
        return;
    }
    const auto& parameters = found->second.parameters;
    const auto typeField = parameters.find("audio.type");
    const auto* type = typeField == parameters.end() ? nullptr
        : std::get_if<std::string>(&typeField->second);
    if (!type || type->find("Generator") == std::string::npos)
    {
        dockWorkspace_.signalPreview().setGeneratorContext(std::move(info));
        return;
    }
    const auto number = [&](std::string_view name, double fallback)
    {
        const auto key = std::string("audio.param.") + std::string(name);
        const auto value = parameters.find(key);
        if (value == parameters.end()) return fallback;
        if (const auto* scalar = std::get_if<double>(&value->second)) return *scalar;
        return fallback;
    };
    const auto choice = [](const char* const* names, std::size_t count, double value)
    {
        const auto index = static_cast<int>(value);
        return index >= 0 && static_cast<std::size_t>(index) < count
            ? juce::String(names[index]) : juce::String("Unknown");
    };
    static constexpr const char* waveforms[]{"Sine", "Square (aliased)", "Triangle (aliased)",
        "Sawtooth (aliased)", "White noise", "Pink noise", "DC"};
    static constexpr const char* curves[]{"linear", "logarithmic", "curved exponential"};
    static constexpr const char* transports[]{"free run", "one-shot", "loop"};
    const auto dot = juce::String::fromUTF8(" · ");
    const auto arrow = juce::String::fromUTF8(" → ");

    if (*type == "Signal Generator")
    {
        const auto waveform = choice(waveforms, std::size(waveforms), number("waveform", 0));
        const auto mode = static_cast<int>(number("mode", 0));
        const auto level = number("levelDb", -60);
        if (mode == 0)
            info.summary = juce::String("Signal Generator") + dot + waveform + dot
                + juce::String(number("frequencyHz", 440), 2) + " Hz" + dot
                + juce::String(level, 1) + " dBFS peak";
        else
        {
            const auto musical = mode == 2;
            const auto curve = choice(curves, std::size(curves), number("sweepCurve", 1));
            info.summary = juce::String("Signal Generator") + dot
                + (musical ? juce::String("Musical sweep") : juce::String(curve + " sweep"))
                + dot + juce::String(number("startFrequencyHz", 20), 1) + arrow
                + juce::String(number("endFrequencyHz", 20000), 1) + " Hz" + dot
                + juce::String(number("durationSeconds", 5), 2) + " s" + dot
                + choice(transports, std::size(transports), number("transport", 0))
                + dot + juce::String(level, 1) + " dBFS peak";
            info.durationSeconds = number("durationSeconds", 5);
            info.transport = static_cast<int>(number("transport", 0));
        }
    }
    else if (*type == "Chirp Generator")
    {
        info.summary = juce::String("Chirp Generator") + dot
            + choice(curves, std::size(curves), number("sweepCurve", 1))
            + dot + juce::String(number("startFrequencyHz", 20), 1) + arrow
            + juce::String(number("endFrequencyHz", 20000), 1) + " Hz" + dot
            + juce::String(number("durationSeconds", 5), 2) + " s" + dot
            + choice(transports, std::size(transports), number("transport", 1))
            + dot + juce::String(number("levelDb", -60), 1) + " dBFS peak";
        info.durationSeconds = number("durationSeconds", 5);
        info.transport = static_cast<int>(number("transport", 1));
    }
    else if (*type == "Noise Generator")
        info.summary = juce::String("Noise Generator") + dot
            + juce::String(number("color", 0) == 1 ? "Pink" : "White")
            + dot + juce::String(number("levelDbRms", -60), 1) + " dBFS peak reference";
    else if (*type == "Burst Generator")
        info.summary = juce::String("Burst Generator") + dot
            + juce::String(number("frequencyHz", 1000), 1) + " Hz" + dot
            + juce::String(number("durationSeconds", 0.1) * 1000, 0) + " ms" + dot
            + juce::String(number("levelDb", -60), 1) + " dBFS peak";
    else if (*type == "Click Generator")
        info.summary = juce::String("Click Generator") + dot
            + juce::String(number("bpm", 120), 1) + " BPM" + dot
            + juce::String(number("frequencyHz", 1000), 1) + " Hz" + dot
            + juce::String(number("levelDb", -60), 1) + " dBFS peak";
    else if (*type == "Impulse Generator")
        info.summary = juce::String("Impulse Generator") + dot + "one-sample peak" + dot
            + juce::String(number("levelDb", -60), 1) + " dBFS";
    else if (*type == "DC Generator")
        info.summary = juce::String("DC Generator") + dot + "exact "
            + juce::String(number("value", 0), 4) + " / " + juce::String::fromUTF8("±1.0");
    dockWorkspace_.signalPreview().setGeneratorContext(std::move(info));
}

void MainComponent::report(const application::Result& result)
{
    showStatus(result.succeeded ? "Project edit accepted."
                                : juce::String::fromUTF8(result.error.c_str()));
    if (result.succeeded)
        refresh();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& result)
{
    result.addArray({rename, undo, redo, toggleInspector, toggleReducedEffects, resetLayout,
                     showModules, showProperties, showAudioSettings, quit,
                     saveProject, saveProjectAs, loadProject, addPatch, startTransport, stopTransport, about});
}

void MainComponent::getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info)
{
    switch (command)
    {
        case startTransport:
            info.setInfo(runtime_->running() ? "Stop (F5)" : "Start (F5)",
                         "Toggle the audio engine; F5 starts or stops according to its current state", "Audio", 0);
            info.setActive(!transportTransitionPending_ && !startFailureDialogPending_);
            info.addDefaultKeypress(juce::KeyPress::F5Key, 0); break;
        case stopTransport:
            info.setInfo("Stop (Shift+F5)", "Stop/quiesce audio and detach visualizer sources", "Audio", 0);
            info.setActive(runtime_->running()); info.addDefaultKeypress(juce::KeyPress::F5Key, juce::ModifierKeys::shiftModifier); break;
        case rename: info.setInfo("Rename workspace", "Change project name", "Workspace", 0); break;
        case saveProject:
        case saveProjectAs:
            info.setInfo(command == saveProject ? "Save Project" : "Save Project As...",
                         "Save a versioned .valab project", "File", 0);
            info.setActive(project_ && !fileWorkflow_);
            info.addDefaultKeypress('s', juce::ModifierKeys::commandModifier
                | (command == saveProjectAs ? juce::ModifierKeys::shiftModifier : 0));
            break;
        case loadProject:
            info.setInfo("Load Project...", "Load a .valab project", "File", 0);
            info.setActive(project_ && !fileWorkflow_);
            info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
            break;
        case addPatch: info.setInfo("New Patch", "Add an empty structural patch", "File", 0); break;
        case undo:
            info.setInfo("Undo", "Undo last workspace or graph edit", "Edit", 0);
            info.setActive(workspace_.canUndo());
            info.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
            break;
        case redo:
            info.setInfo("Redo", "Redo last workspace or graph edit", "Edit", 0);
            info.setActive(workspace_.canRedo());
            info.addDefaultKeypress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case toggleInspector:
            info.setInfo("Show Property Inspector", "Show properties, script editor and signal inspection", "View", 0);
            info.setTicked(inspectorVisible());
            break;
        case toggleReducedEffects:
            info.setInfo("Reduced effects (no glow)", "Disable decorative selection glow while keeping crisp state outlines", "View", 0);
            info.setTicked(!ShellTheme::glowEnabled());
            break;
        case resetLayout: info.setInfo("Reset Panels", "Restore the fixed workspace panels", "View", 0); break;
        case showModules: info.setInfo("Module Browser", "Browse and insert executable modules", "View", 0); break;
        case showProperties: info.setInfo("Property Inspector", "Show parameters, scripts and signal inspection", "View", 0); break;
        case showAudioSettings: info.setInfo("Audio Setup", "Configure audio devices and capabilities. Playback remains explicit.", "Audio", 0); break;
        case about: info.setInfo("About Visual Audio Lab", "Show application logo and build information", "Help", 0); break;
        case quit:
            info.setInfo("Quit", "Close Visual Audio Lab", "File", 0);
            info.addDefaultKeypress('q', juce::ModifierKeys::commandModifier);
            break;
        default: break;
    }
    // Focused TextEditor consumes native editing shortcuts before the shell's key mappings.
}

bool MainComponent::perform(const InvocationInfo& invocation)
{
    switch (invocation.commandID)
    {
        case startTransport:
            toggleTransportAction(); return true;
        case stopTransport:
            stopTransportAction(); return true;
        case rename: report(workspace_.rename(toolbar_.workspaceName().toStdString())); return true;
        case undo: captureProjectView(); report(workspace_.undo()); return true;
        case redo: captureProjectView(); report(workspace_.redo()); return true;
        case toggleInspector: setInspectorVisible(!inspectorVisible()); return true;
        case toggleReducedEffects:
            ShellTheme::setGlowEnabled(!ShellTheme::glowEnabled());
            dockWorkspace_.canvas().repaint();
            menuItemsChanged();
            return true;
        case resetLayout: dockWorkspace_.resetLayout(); return true;
        case showModules: dockWorkspace_.showPanel(PanelId::moduleBrowser); return true;
        case showProperties: dockWorkspace_.showPanel(PanelId::propertyInspector); return true;
        case showAudioSettings: showAudioSetup(); return true;
        case about: showAbout(); return true;
        case saveProject: chooseSave(false); return true;
        case saveProjectAs: chooseSave(true); return true;
        case loadProject: chooseLoad(); return true;
        case addPatch: addPatch_.triggerClick(); return true;
        case quit:
            if (auto* app = juce::JUCEApplication::getInstance())
            {
                app->systemRequestedQuit();
                return true;
            }
            return false;
        default: return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return {"File", "Edit", "View", "Audio", "Help"};
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addCommandItem(&commands_, loadProject);
        menu.addCommandItem(&commands_, saveProject);
        menu.addCommandItem(&commands_, saveProjectAs);
        juce::PopupMenu recent;
        recentMenu_ = project_ ? project_->recent() : std::vector<std::filesystem::path>{};
        for (std::size_t i = 0; i < recentMenu_.size(); ++i)
        {
            std::error_code error;
            const auto exists = std::filesystem::is_regular_file(recentMenu_[i], error);
            const auto utf8 = recentMenu_[i].u8string();
            auto label = juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size()));
            if (!exists) label += " (missing/unavailable)";
            recent.addItem(0x4000 + static_cast<int>(i), label, !fileWorkflow_);
            if (!exists)
                recent.addItem(0x4100 + static_cast<int>(i), "Remove missing entry: " + label, !fileWorkflow_);
        }
        if (recentMenu_.empty()) recent.addItem(1, "No recent projects", false);
        menu.addSubMenu("Recent Files", recent);
        menu.addCommandItem(&commands_, addPatch);
        menu.addSeparator();
        menu.addCommandItem(&commands_, quit);
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commands_, rename);
        menu.addCommandItem(&commands_, undo);
        menu.addCommandItem(&commands_, redo);
    }
    else if (index == 2)
    {
        for (auto id : {showModules, showProperties})
            menu.addCommandItem(&commands_, id);
        menu.addSeparator();
        menu.addCommandItem(&commands_, toggleInspector);
        menu.addCommandItem(&commands_, toggleReducedEffects);
        menu.addCommandItem(&commands_, resetLayout);
    }
    else if (index == 3)
    {
        menu.addCommandItem(&commands_, startTransport);
        menu.addCommandItem(&commands_, stopTransport);
        menu.addSeparator();
        menu.addCommandItem(&commands_, showAudioSettings);
    }
    else if (index == 4)
        menu.addCommandItem(&commands_, about);
    return menu;
}

void MainComponent::updateProjectChrome()
{
    patches_.clear(juce::dontSendNotification);
    int item{1}, selected{1};
    for (const auto& [id, patch] : workspace_.project().patches)
    {
        patches_.addItem(juce::String::fromUTF8(patch.name.c_str()) + " (#" + juce::String(id) + ")", item);
        if (id == workspace_.project().activePatch) selected = item;
        ++item;
    }
    patches_.setSelectedId(selected, juce::dontSendNotification);
    auto title = juce::String::fromUTF8(workspace_.workspace().name.c_str())
        + (project_ && project_->dirty() ? " *" : "");
    if (project_ && !project_->path().empty())
    {
        const auto filename = project_->path().filename().u8string();
        title += " - " + juce::String::fromUTF8(reinterpret_cast<const char*>(filename.data()),
                                              static_cast<int>(filename.size()));
    }
    title += " - Visual Audio Lab";
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName(title);
}

void MainComponent::captureProjectView()
{
    dockWorkspace_.canvas().cancelGesture();
    if (displayedPatch_ == workspace_.project().activePatch)
        workspace_.setViewport(dockWorkspace_.canvas().navigation());
    auto layout = dockWorkspace_.projectLayout();
    if (const auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        const auto bounds = window->isFullScreen() && window->getPeer()
            ? window->getPeer()->getNonFullScreenBounds() : window->getBounds();
        layout.x = bounds.getX(); layout.y = bounds.getY();
        layout.width = bounds.getWidth(); layout.height = bounds.getHeight();
    }
    else
    {
        const auto& previous = workspace_.project().layout;
        layout.x = previous.x; layout.y = previous.y;
        layout.width = previous.width; layout.height = previous.height;
    }
    workspace_.setLayout(layout);
}

void MainComponent::restoreProjectView()
{
    dockWorkspace_.visualizations().reset();
    const auto& layout = workspace_.project().layout;
    dockWorkspace_.restoreLayout(layout);
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        juce::Rectangle<int> desired(layout.x, layout.y, layout.width, layout.height);
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* display = displayForRestoredBounds(displays, desired))
        {
            const auto placement = fitWindowToUserArea(desired, display->userArea);
            window->setResizeLimits(placement.minimumWidth, placement.minimumHeight,
                                    placement.maximumWidth, placement.maximumHeight);
            desired = placement.bounds;
        }
        window->setFullScreen(false);
        window->setBounds(desired);
    }
    displayedPatch_ = workspace_.project().activePatch;
    dockWorkspace_.canvas().resetForProject();
    refresh();
}

void MainComponent::fileResult(const application::Result& result, const juce::String& success)
{
    auto message = result.succeeded ? success : juce::String::fromUTF8(result.error.c_str());
    if (result.succeeded && project_ && !project_->warning().empty())
        message += " Recent Files warning: " + juce::String::fromUTF8(project_->warning().c_str());
    showStatus(message);
    updateProjectChrome();
    commands_.commandStatusChanged();
    menuItemsChanged();
}

application::Result MainComponent::saveProjectTo(const std::filesystem::path& path)
{
    if (!project_) return application::Result::failure("Project storage is unavailable.");
    captureProjectView();
    const auto result = project_->saveAs(path);
    fileResult(result, "Project saved.");
    return result;
}

application::Result MainComponent::loadProjectFrom(const std::filesystem::path& path)
{
    if (!project_) return application::Result::failure("Project storage is unavailable.");
    dockWorkspace_.canvas().cancelGesture();
    const auto stopped = runtime_->stop();
    if (!stopped.succeeded) return stopped;
    const auto result = project_->load(path);
    if (result.succeeded) restoreProjectView();
    fileResult(result, "Project loaded. Transport stopped; Start is explicit.");
    return result;
}

void MainComponent::chooseSave(bool saveAs, std::function<void()> continuation)
{
    if (!project_ || fileWorkflow_) return;
    if (!saveAs && !project_->path().empty())
    {
        if (saveProjectTo(project_->path()).succeeded && continuation) continuation();
        return;
    }
    fileWorkflow_ = true;
    const auto utf8 = project_->path().u8string();
    const juce::File initial = utf8.empty() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.valab")
        : juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size())));
    chooser_ = std::make_unique<juce::FileChooser>("Save Visual Audio Lab Project", initial, "*.valab", true);
    const juce::Component::SafePointer<MainComponent> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, continuation = std::move(continuation)](const juce::FileChooser& chooser)
        {
            if (!safe) return;
            const auto chosen = chooser.getResult();
            safe->fileWorkflow_ = false;
            if (chosen == juce::File{}) { safe->fileResult(application::Result::success(), "Save cancelled."); return; }
            const auto name = chosen.getFullPathName().toStdString();
            const auto result = safe->saveProjectTo(std::filesystem::path(std::u8string(name.begin(), name.end())));
            if (result.succeeded && continuation) continuation();
        });
}

void MainComponent::confirmReplace(std::function<void()> continuation)
{
    if (fileWorkflow_) return;
    captureProjectView();
    if (!project_ || !project_->dirty()) { continuation(); return; }
    fileWorkflow_ = true;
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showYesNoCancelBox(juce::AlertWindow::WarningIcon, "Unsaved project",
        "Save changes before replacing or closing this project?", "Save", "Discard", "Cancel",
        getTopLevelComponent(), juce::ModalCallbackFunction::create([safe, continuation = std::move(continuation)](int answer)
        {
            if (!safe) return;
            safe->fileWorkflow_ = false;
            if (answer == 1) safe->chooseSave(false, continuation);
            else if (answer == 2) continuation();
            else safe->fileResult(application::Result::success(), "Operation cancelled; project retained.");
        }));
}

void MainComponent::requestClose(std::function<void()> continuation) { confirmReplace(std::move(continuation)); }

void MainComponent::loadWithConsent(std::filesystem::path path)
{
    const juce::Component::SafePointer<MainComponent> safe(this);
    confirmReplace([safe, path = std::move(path)]
    {
        if (safe)
        {
            const auto result = safe->loadProjectFrom(path);
            if (!result.succeeded)
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Project not loaded",
                    juce::String::fromUTF8(result.error.c_str()), "OK", safe->getTopLevelComponent());
        }
    });
}

void MainComponent::chooseLoad()
{
    if (!project_ || fileWorkflow_) return;
    fileWorkflow_ = true;
    chooser_ = std::make_unique<juce::FileChooser>("Load Visual Audio Lab Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.valab", true);
    const juce::Component::SafePointer<MainComponent> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser)
        {
            if (!safe) return;
            const auto chosen = chooser.getResult();
            safe->fileWorkflow_ = false;
            if (chosen == juce::File{}) { safe->fileResult(application::Result::success(), "Load cancelled."); return; }
            const auto name = chosen.getFullPathName().toStdString();
            safe->loadWithConsent(std::filesystem::path(std::u8string(name.begin(), name.end())));
        });
}

void MainComponent::menuItemSelected(int id, int)
{
    if (!project_ || fileWorkflow_) return;
    const bool remove = id >= 0x4100 && id < 0x4100 + static_cast<int>(recentMenu_.size());
    const auto index = id - (remove ? 0x4100 : 0x4000);
    if (index < 0 || index >= static_cast<int>(recentMenu_.size())) return;
    if (remove) fileResult(project_->forgetRecent(recentMenu_[index]), "Recent Files entry removed.");
    else loadWithConsent(recentMenu_[index]);
}
}
