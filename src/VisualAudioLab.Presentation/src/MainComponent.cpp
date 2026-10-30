#include <val/presentation/MainComponent.h>
#include <ValResources.h>

namespace val::presentation
{
MainComponent::MainComponent(application::WorkspaceService& workspace) : workspace_(workspace)
{
    setName("Visual Audio Lab workbench skeleton");
    setWantsKeyboardFocus(true);
    banner_.setText(juce::String::fromUTF8(val_resources::ShellNotice_txt,
                                          val_resources::ShellNotice_txtSize),
                    juce::dontSendNotification);
    banner_.setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
    explorer_.setText("Project Explorer\nNo project system implemented.", juce::dontSendNotification);
    toolbox_.setText("Toolbox\nNo node modules available.", juce::dontSendNotification);
    canvas_.setText("Patch workspace\nEditor implementation is deferred.", juce::dontSendNotification);
    properties_.setText("Properties\nNo selected component.", juce::dontSendNotification);
    inspector_.setText("Signal Inspector: unavailable - no processing.", juce::dontSendNotification);
    workspaceName_.setTitle("Temporary workspace name");
    workspaceName_.setDescription("Rename this in-memory workspace. No project file is saved.");
    workspaceName_.setText(juce::String::fromUTF8(workspace_.workspace().name.c_str()));

    for (auto* component : std::initializer_list<juce::Component*>{
             &menu_, &banner_, &explorer_, &toolbox_, &canvas_, &properties_, &inspector_,
             &status_, &workspaceName_, &renameButton_, &undoButton_, &redoButton_})
        addAndMakeVisible(component);

    commands_.registerAllCommandsForTarget(this);
    commands_.setFirstCommandTarget(this);
    addKeyListener(commands_.getKeyMappings());
    renameButton_.setCommandToTrigger(&commands_, rename, true);
    undoButton_.setCommandToTrigger(&commands_, undo, true);
    redoButton_.setCommandToTrigger(&commands_, redo, true);
    showStatus("Ready. Settings/logging are local frameworks; no audio backend exists.");
    refresh();
}

MainComponent::~MainComponent()
{
    removeKeyListener(commands_.getKeyMappings());
    menu_.setModel(nullptr);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff20252b));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    menu_.setBounds(bounds.removeFromTop(26));
    banner_.setBounds(bounds.removeFromTop(54));
    auto toolbar = bounds.removeFromTop(36);
    workspaceName_.setBounds(toolbar.removeFromLeft(200).reduced(2));
    renameButton_.setBounds(toolbar.removeFromLeft(170).reduced(2));
    undoButton_.setBounds(toolbar.removeFromLeft(80).reduced(2));
    redoButton_.setBounds(toolbar.removeFromLeft(80).reduced(2));
    status_.setBounds(bounds.removeFromBottom(44));
    if (inspectorVisible_)
        inspector_.setBounds(bounds.removeFromBottom(56));
    auto left = bounds.removeFromLeft(180);
    explorer_.setBounds(left.removeFromTop(left.getHeight() / 2));
    toolbox_.setBounds(left);
    properties_.setBounds(bounds.removeFromRight(190));
    canvas_.setBounds(bounds);
}

void MainComponent::showStatus(const juce::String& text)
{
    status_.setText(text, juce::dontSendNotification);
    status_.setTooltip(text);
}

void MainComponent::setInspectorVisible(bool visible)
{
    inspectorVisible_ = visible;
    inspector_.setVisible(visible);
    commands_.commandStatusChanged();
    resized();
}

void MainComponent::refresh()
{
    workspaceName_.setText(juce::String::fromUTF8(workspace_.workspace().name.c_str()), false);
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
    result.addArray({rename, undo, redo, toggleInspector, resetLayout});
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
            info.setInfo("Signal Inspector", "Show or hide the placeholder inspector", "View", 0);
            info.setTicked(inspectorVisible_);
            break;
        case resetLayout: info.setInfo("Reset Layout", "Restore shell panels", "View", 0); break;
        default: break;
    }
    // TextEditor retains its native undo/redo shortcuts; menu/buttons target workspace history.
}

bool MainComponent::perform(const InvocationInfo& invocation)
{
    switch (invocation.commandID)
    {
        case rename: report(workspace_.rename(workspaceName_.getText().toStdString())); return true;
        case undo: report(workspace_.undo()); return true;
        case redo: report(workspace_.redo()); return true;
        case toggleInspector: setInspectorVisible(!inspectorVisible_); return true;
        case resetLayout: setInspectorVisible(true); return true;
        default: return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return {"Edit", "View"};
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addCommandItem(&commands_, rename);
        menu.addCommandItem(&commands_, undo);
        menu.addCommandItem(&commands_, redo);
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commands_, toggleInspector);
        menu.addCommandItem(&commands_, resetLayout);
    }
    return menu;
}
}
