#include <val/presentation/MainComponent.h>
#include <ValResources.h>

namespace val::presentation
{
MainComponent::MainComponent(application::WorkspaceService& workspace, application::ProjectSession* project)
    : workspace_(workspace), project_(project)
{
    setLookAndFeel(&theme_);
    setName("Visual Audio Lab application shell");
    setWantsKeyboardFocus(true);
    banner_.setText(juce::String::fromUTF8(val_resources::ShellNotice_txt, val_resources::ShellNotice_txtSize),
                   juce::dontSendNotification);
    banner_.setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
    banner_.setTooltip("Project files and structural editing work. No audio devices or processing.");

    for (auto* component : std::initializer_list<juce::Component*>{
             &menu_, &toolbar_, &banner_, &status_, &dockWorkspace_, &patches_, &addPatch_})
        addAndMakeVisible(component);
    patches_.setName("Active patch");
    patches_.setTooltip("Switch patches without losing their graph or undo history.");
    patches_.onChange = [this]
    {
        const auto index = patches_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(workspace_.project().patches.size()))
            return;
        captureProjectView();
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
    addKeyListener(commands_.getKeyMappings());
    dockWorkspace_.onLayoutChanged = [this]
    {
        commands_.commandStatusChanged();
        menuItemsChanged();
        showStatus("Layout changed. Save Project to retain it.");
    };
    dockWorkspace_.canvas().onEdit = [this](const application::Result& result)
    {
        showStatus(result.succeeded ? "Structural graph edit accepted. No processing."
                                    : juce::String::fromUTF8(result.error.c_str()));
        commands_.commandStatusChanged();
        menuItemsChanged();
        if (displayedPatch_ != workspace_.project().activePatch)
            refresh();
        else
            updateProjectChrome();
    };
    showStatus("Ready. Settings/logging are local frameworks; no audio backend exists.");
    refresh();
    if (project_ && !project_->warning().empty())
        showStatus(juce::String::fromUTF8(project_->warning().c_str()));
}

MainComponent::~MainComponent()
{
    removeKeyListener(commands_.getKeyMappings());
    menu_.setModel(nullptr);
    dockWorkspace_.onLayoutChanged = nullptr;
    dockWorkspace_.canvas().onEdit = nullptr;
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::background());
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    menu_.setBounds(bounds.removeFromTop(26));
    toolbar_.setBounds(bounds.removeFromTop(40));
    auto info = bounds.removeFromTop(38);
    addPatch_.setBounds(info.removeFromRight(95).reduced(2, 4));
    patches_.setBounds(info.removeFromRight(170).reduced(2, 4));
    banner_.setBounds(info);
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
        displayedPatch_ = workspace_.project().activePatch;
        dockWorkspace_.canvas().resetForProject();
    }
    updateProjectChrome();
    commands_.commandStatusChanged();
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
    result.addArray({rename, undo, redo, toggleInspector, resetLayout,
                     showCanvas, showModules, showExplorer, showProperties, showAudioSettings, quit,
                     saveProject, saveProjectAs, loadProject, addPatch});
}

void MainComponent::getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info)
{
    switch (command)
    {
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
            info.setInfo("Show Property Inspector", "Show or hide the placeholder inspector", "View", 0);
            info.setTicked(inspectorVisible());
            break;
        case resetLayout: info.setInfo("Reset Layout", "Restore shell panels", "View", 0); break;
        case showCanvas: info.setInfo("Patch Canvas", "Focus the patch placeholder", "View", 0); break;
        case showModules: info.setInfo("Module Browser", "Show the module placeholder", "View", 0); break;
        case showExplorer: info.setInfo("Project Explorer", "Show the project placeholder", "View", 0); break;
        case showProperties: info.setInfo("Property Inspector", "Show the properties placeholder", "View", 0); break;
        case showAudioSettings: info.setInfo("Audio Settings", "Show unavailable audio settings", "View", 0); break;
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
        case rename: report(workspace_.rename(toolbar_.workspaceName().toStdString())); return true;
        case undo: captureProjectView(); report(workspace_.undo()); return true;
        case redo: captureProjectView(); report(workspace_.redo()); return true;
        case toggleInspector: setInspectorVisible(!inspectorVisible()); return true;
        case resetLayout: dockWorkspace_.resetLayout(); return true;
        case showCanvas: dockWorkspace_.showPanel(PanelId::patchCanvas); return true;
        case showModules: dockWorkspace_.showPanel(PanelId::moduleBrowser); return true;
        case showExplorer: dockWorkspace_.showPanel(PanelId::projectExplorer); return true;
        case showProperties: dockWorkspace_.showPanel(PanelId::propertyInspector); return true;
        case showAudioSettings: dockWorkspace_.showPanel(PanelId::audioSettings); return true;
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
    return {"File", "Edit", "View", "Audio"};
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
        for (auto id : {showCanvas, showModules, showExplorer, showProperties, showAudioSettings})
            menu.addCommandItem(&commands_, id);
        menu.addSeparator();
        menu.addCommandItem(&commands_, toggleInspector);
        menu.addCommandItem(&commands_, resetLayout);
    }
    else if (index == 3)
    {
        menu.addItem(4, "Start Audio (no backend)", false);
        menu.addItem(5, "Stop Audio (no backend)", false);
        menu.addSeparator();
        menu.addCommandItem(&commands_, showAudioSettings);
    }
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
    const auto& layout = workspace_.project().layout;
    dockWorkspace_.restoreLayout(layout);
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        juce::Rectangle<int> desired(layout.x, layout.y, layout.width, layout.height);
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* display = displays.getDisplayForRect(desired))
        {
            const auto area = display->userArea;
            desired.setSize(juce::jmin(desired.getWidth(), area.getWidth()), juce::jmin(desired.getHeight(), area.getHeight()));
            desired.setPosition(juce::jlimit(area.getX(), area.getRight() - desired.getWidth(), desired.getX()),
                                juce::jlimit(area.getY(), area.getBottom() - desired.getHeight(), desired.getY()));
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
    const auto result = project_->load(path);
    if (result.succeeded) restoreProjectView();
    fileResult(result, "Project loaded. No audio processing.");
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
