#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/**
 * Compact command-bound toolbar; the command manager is borrowed and outlives this view.
 * Renaming edits project identity; saving remains an explicit File command.
 */
class ShellToolbar final : public juce::Component
{
public:
    ShellToolbar(juce::ApplicationCommandManager& commands, juce::CommandID rename,
                 juce::CommandID undo, juce::CommandID redo,
                 juce::CommandID audioSettings, juce::CommandID resetLayout);
    void resized() override;
    [[nodiscard]] juce::String workspaceName() const { return workspaceName_.getText(); }
    void setWorkspaceName(const juce::String& name) { workspaceName_.setText(name, false); }

private:
    juce::TextEditor workspaceName_;
    juce::TextButton rename_{"Rename workspace"}, undo_{"Undo"}, redo_{"Redo"};
    juce::TextButton audioSettings_{"Audio Settings"}, resetLayout_{"Reset Layout"};
};

/** Persistent status and capability disclosure, independent of any audio-engine state. */
class ShellStatusBar final : public juce::Component
{
public:
    ShellStatusBar();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setMessage(const juce::String& message);

private:
    juce::Label message_, capability_;
};
}
