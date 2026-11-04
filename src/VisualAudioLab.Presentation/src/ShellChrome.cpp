#include <val/presentation/ShellChrome.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>

namespace val::presentation
{
ShellToolbar::ShellToolbar(juce::ApplicationCommandManager& commands, juce::CommandID rename,
                           juce::CommandID undo, juce::CommandID redo,
                           juce::CommandID resetLayout)
{
    setName("Application toolbar");
    workspaceName_.setTitle("Temporary workspace name");
    workspaceName_.setFont(Typography::font(TextRole::applicationTitle));
    workspaceName_.setDescription("Project name. Rename applies an undoable edit; File saves the project.");
    rename_.setCommandToTrigger(&commands, rename, true);
    undo_.setCommandToTrigger(&commands, undo, true);
    redo_.setCommandToTrigger(&commands, redo, true);
    resetLayout_.setCommandToTrigger(&commands, resetLayout, true);
    for (auto* child : std::initializer_list<juce::Component*>{
             &workspaceName_, &rename_, &undo_, &redo_, &resetLayout_})
        addAndMakeVisible(child);
}

void ShellToolbar::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    if (bounds.getWidth() < 610)
    {
        auto identity = bounds.removeFromTop(30);
        const auto nameWidth = juce::jmax(140, identity.getWidth() * 45 / 100);
        workspaceName_.setBounds(identity.removeFromLeft(nameWidth).reduced(2));
        rename_.setBounds(identity.reduced(2));
        auto actions = bounds.removeFromTop(30);
        const auto cell = actions.getWidth() / 3;
        undo_.setBounds(actions.removeFromLeft(cell).reduced(2));
        redo_.setBounds(actions.removeFromLeft(cell).reduced(2));
        resetLayout_.setBounds(actions.reduced(2));
        return;
    }
    workspaceName_.setBounds(bounds.removeFromLeft(150).reduced(2));
    rename_.setBounds(bounds.removeFromLeft(142).reduced(2));
    undo_.setBounds(bounds.removeFromLeft(56).reduced(2));
    redo_.setBounds(bounds.removeFromLeft(56).reduced(2));
    resetLayout_.setBounds(bounds.removeFromLeft(110).reduced(2));
}

ShellStatusBar::ShellStatusBar()
{
    setName("Application status bar");
    message_.setTitle("Application status");
    message_.setFont(Typography::font(TextRole::status));
    capability_.setFont(Typography::font(TextRole::status));
    capability_.setText("Explicit transport | No auto playback", juce::dontSendNotification);
    capability_.setColour(juce::Label::textColourId, ShellTheme::textSecondary());
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
