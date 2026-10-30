#include <val/presentation/MainComponent.h>
#include <ValResources.h>

namespace val::presentation
{
MainComponent::MainComponent(application::WorkspaceService& workspace) : workspace_(workspace)
{
    setLookAndFeel(&theme_);
    setName("Visual Audio Lab application shell");
    setWantsKeyboardFocus(true);
    banner_.setText(juce::String::fromUTF8(val_resources::ShellNotice_txt,
                                          val_resources::ShellNotice_txtSize),
                    juce::dontSendNotification);
    banner_.setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
    banner_.setTooltip("Panels and layout controls work; product operations are unavailable.");

    for (auto* component : std::initializer_list<juce::Component*>{
             &menu_, &toolbar_, &banner_, &status_, &dockWorkspace_})
        addAndMakeVisible(component);

    commands_.registerAllCommandsForTarget(this);
    commands_.setFirstCommandTarget(this);
    addKeyListener(commands_.getKeyMappings());
    dockWorkspace_.onLayoutChanged = [this]
    {
        commands_.commandStatusChanged();
        menuItemsChanged();
        showStatus("Layout changed (temporary). Use View to reopen hidden panels.");
    };
    showStatus("Ready. Settings/logging are local frameworks; no audio backend exists.");
    refresh();
}

MainComponent::~MainComponent()
{
    removeKeyListener(commands_.getKeyMappings());
    menu_.setModel(nullptr);
    dockWorkspace_.onLayoutChanged = nullptr;
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
    banner_.setBounds(bounds.removeFromTop(38));
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
    commands_.commandStatusChanged();
}

void MainComponent::report(const application::Result& result)
{
    showStatus(result.succeeded ? "Workspace command accepted. Changes are temporary."
                                : juce::String::fromUTF8(result.error.c_str()));
    if (result.succeeded)
        refresh();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& result)
{
    result.addArray({rename, undo, redo, toggleInspector, resetLayout,
                     showCanvas, showModules, showExplorer, showProperties, showAudioSettings, quit});
}

void MainComponent::getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info)
{
    switch (command)
    {
        case rename: info.setInfo("Rename workspace", "Change temporary workspace identity", "Workspace", 0); break;
        case undo:
            info.setInfo("Undo", "Undo last workspace command", "Edit", 0);
            info.setActive(workspace_.canUndo());
            break;
        case redo:
            info.setInfo("Redo", "Redo last workspace command", "Edit", 0);
            info.setActive(workspace_.canRedo());
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
    // TextEditor retains its native undo/redo shortcuts; menu/buttons target workspace history.
}

bool MainComponent::perform(const InvocationInfo& invocation)
{
    switch (invocation.commandID)
    {
        case rename: report(workspace_.rename(toolbar_.workspaceName().toStdString())); return true;
        case undo: report(workspace_.undo()); return true;
        case redo: report(workspace_.redo()); return true;
        case toggleInspector: setInspectorVisible(!inspectorVisible()); return true;
        case resetLayout: dockWorkspace_.resetLayout(); return true;
        case showCanvas: dockWorkspace_.showPanel(PanelId::patchCanvas); return true;
        case showModules: dockWorkspace_.showPanel(PanelId::moduleBrowser); return true;
        case showExplorer: dockWorkspace_.showPanel(PanelId::projectExplorer); return true;
        case showProperties: dockWorkspace_.showPanel(PanelId::propertyInspector); return true;
        case showAudioSettings: dockWorkspace_.showPanel(PanelId::audioSettings); return true;
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
        menu.addItem(1, "New Project (not implemented)", false);
        menu.addItem(2, "Open Project (not implemented)", false);
        menu.addItem(3, "Save Project (not implemented)", false);
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
}
