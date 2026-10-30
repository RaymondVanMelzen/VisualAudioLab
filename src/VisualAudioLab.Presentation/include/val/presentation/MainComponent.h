#pragma once

#include <val/application/WorkspaceService.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/**
 * Message-thread shell and command adapter; named regions are nonfunctional placeholders.
 * The borrowed workspace service outlives this component. JUCE command routing is kept in
 * Presentation; reversible use cases and history remain framework-free in Application.
 */
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel
{
public:
    explicit MainComponent(application::WorkspaceService& workspace);
    ~MainComponent() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void showStatus(const juce::String& text);
    [[nodiscard]] bool inspectorVisible() const noexcept { return inspectorVisible_; }
    void setInspectorVisible(bool visible);

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int, int) override {}

private:
    /** Stable, presentation-only command identities, separate from persisted user content. */
    enum CommandId { rename = 0x2100, undo, redo, toggleInspector, resetLayout };
    void refresh();
    void report(const application::Result& result);
    application::WorkspaceService& workspace_;
    juce::ApplicationCommandManager commands_;
    juce::MenuBarComponent menu_{this};
    juce::Label banner_, explorer_, toolbox_, canvas_, properties_, inspector_, status_;
    juce::TextEditor workspaceName_;
    juce::TextButton renameButton_{"Rename workspace"}, undoButton_{"Undo"}, redoButton_{"Redo"};
    bool inspectorVisible_{true};
};
}
