#pragma once

#include <val/application/WorkspaceService.h>
#include <val/application/ProjectSession.h>
#include "DockWorkspace.h"
#include "ShellChrome.h"
#include "ShellTheme.h"

namespace val::presentation
{
/**
 * Message-thread desktop shell and command adapter with a structural canvas and docked tools.
 * The borrowed workspace service outlives this component. JUCE command routing is kept in
 * Presentation; reversible use cases and history remain framework-free in Application.
 */
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel
{
public:
    explicit MainComponent(application::WorkspaceService& workspace, application::ProjectSession* project = nullptr);
    ~MainComponent() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void showStatus(const juce::String& text);
    /** Existing local preference controls the Property Inspector shared signal preview. */
    [[nodiscard]] bool inspectorVisible() const noexcept;
    void setInspectorVisible(bool visible);
    [[nodiscard]] DockWorkspace& dockWorkspace() noexcept { return dockWorkspace_; }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID command, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int id, int) override;
    /** Used by native close and Quit; cancellation/failure never invokes the continuation. */
    void requestClose(std::function<void()> continuation);
    /** Saves to a chosen path after any required UI path selection. */
    [[nodiscard]] application::Result saveProjectTo(const std::filesystem::path& path);
    /** Loads a chosen path; the caller must first obtain consent to discard dirty content. */
    [[nodiscard]] application::Result loadProjectFrom(const std::filesystem::path& path);

private:
    /** Stable, presentation-only command identities, separate from persisted user content. */
    enum CommandId
    {
        rename = 0x2100, undo, redo, toggleInspector, resetLayout,
        showCanvas, showModules, showExplorer, showProperties, showAudioSettings, quit,
        saveProject, saveProjectAs, loadProject, addPatch
    };
    void refresh();
    void report(const application::Result& result);
    void captureProjectView();
    void restoreProjectView();
    void fileResult(const application::Result& result, const juce::String& success);
    void chooseSave(bool saveAs, std::function<void()> continuation = {});
    void chooseLoad();
    void confirmReplace(std::function<void()> continuation);
    void loadWithConsent(std::filesystem::path path);
    void updateProjectChrome();
    application::WorkspaceService& workspace_;
    application::ProjectSession* project_{};
    std::unique_ptr<juce::FileChooser> chooser_;
    bool fileWorkflow_{};
    domain::GraphId displayedPatch_{1};
    std::vector<std::filesystem::path> recentMenu_;
    ShellTheme theme_;
    juce::ApplicationCommandManager commands_;
    juce::MenuBarComponent menu_{this};
    ShellToolbar toolbar_{commands_, rename, undo, redo, showAudioSettings, resetLayout};
    juce::Label banner_;
    juce::ComboBox patches_;
    juce::TextButton addPatch_{"New Patch"};
    ShellStatusBar status_;
    DockWorkspace dockWorkspace_{workspace_};
};
}
