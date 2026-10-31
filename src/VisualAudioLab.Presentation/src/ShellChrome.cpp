#include <val/presentation/ShellChrome.h>
#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
ShellToolbar::ShellToolbar(juce::ApplicationCommandManager& commands, juce::CommandID rename,
                           juce::CommandID undo, juce::CommandID redo,
                           juce::CommandID audioSettings, juce::CommandID resetLayout)
{
    setName("Application toolbar");
    workspaceName_.setTitle("Temporary workspace name");
    workspaceName_.setDescription("Project name. Rename applies an undoable edit; File saves the project.");
    rename_.setCommandToTrigger(&commands, rename, true);
    undo_.setCommandToTrigger(&commands, undo, true);
    redo_.setCommandToTrigger(&commands, redo, true);
    audioSettings_.setCommandToTrigger(&commands, audioSettings, true);
    resetLayout_.setCommandToTrigger(&commands, resetLayout, true);
    for (auto* child : std::initializer_list<juce::Component*>{
             &workspaceName_, &rename_, &undo_, &redo_, &audioSettings_, &resetLayout_})
        addAndMakeVisible(child);
}

void ShellToolbar::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    workspaceName_.setBounds(bounds.removeFromLeft(150).reduced(2));
    rename_.setBounds(bounds.removeFromLeft(142).reduced(2));
    undo_.setBounds(bounds.removeFromLeft(56).reduced(2));
    redo_.setBounds(bounds.removeFromLeft(56).reduced(2));
    audioSettings_.setBounds(bounds.removeFromLeft(120).reduced(2));
    resetLayout_.setBounds(bounds.removeFromLeft(116).reduced(2));
}

ShellStatusBar::ShellStatusBar()
{
    setName("Application status bar");
    message_.setTitle("Application status");
    capability_.setText("Explicit transport | No auto playback", juce::dontSendNotification);
    capability_.setColour(juce::Label::textColourId, ShellTheme::accent());
    capability_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(message_);
    addAndMakeVisible(capability_);
}

void ShellStatusBar::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::surface());
}

void ShellStatusBar::resized()
{
    auto bounds = getLocalBounds().reduced(6, 2);
    capability_.setBounds(bounds.removeFromRight(210));
    message_.setBounds(bounds);
}

void ShellStatusBar::setMessage(const juce::String& message)
{
    message_.setText(message, juce::dontSendNotification);
    message_.setTooltip(message);
}
}
