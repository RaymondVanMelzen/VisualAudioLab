#pragma once

#include <val/application/WorkspaceService.h>
#include "DockWorkspace.h"
#include "ShellChrome.h"
#include "ShellTheme.h"

namespace val::presentation
{
/**
 * Message-thread desktop shell and command adapter with transient dockable placeholders.
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
    /** Existing local preference now controls the Property Inspector placeholder. */
    [[nodiscard]] bool inspectorVisible() const noexcept;
    void setInspectorVisible(bool visible);
    [[nodiscard]] DockWorkspace& dockWorkspace() noexcept { return dockWorkspace_; }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int, int) override {}

private:
    /** Stable, presentation-only command identities, separate from persisted user content. */
    enum CommandId
    {
        rename = 0x2100, undo, redo, toggleInspector, resetLayout,
        showCanvas, showModules, showExplorer, showProperties, showAudioSettings, quit
    };
    void refresh();
    void report(const application::Result& result);
    application::WorkspaceService& workspace_;
    ShellTheme theme_;
    juce::ApplicationCommandManager commands_;
    juce::MenuBarComponent menu_{this};
    ShellToolbar toolbar_{commands_, rename, undo, redo, showAudioSettings, resetLayout};
    juce::Label banner_;
    ShellStatusBar status_;
    DockWorkspace dockWorkspace_;
};
}
