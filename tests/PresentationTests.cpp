#include "TestSupport.h"
#include <val/presentation/MainComponent.h>

/** Locates nested framework controls without depending on toolbar implementation details. */
template <typename ComponentType>
ComponentType* findControl(juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* control = dynamic_cast<ComponentType*>(child))
            return control;
        if (auto* nested = findControl<ComponentType>(*child))
            return nested;
    }
    return nullptr;
}

void runNodeEditorTests();
void runLargeGraphBenchmark();
void runProjectPresentationTests();
void runVisualizationPresentationTests();
void runRuntimeIntegrationTests();
void runModulePanelIntegrationTests();

int main(int argc, char** argv)
{
    if (argc == 2)
        return runTests([&]
        {
            const juce::ScopedJuceInitialiser_GUI initialise;
            if (std::string_view(argv[1]) == "--node-editor")
                runNodeEditorTests();
            else if (std::string_view(argv[1]) == "--benchmark")
                runLargeGraphBenchmark();
            else if (std::string_view(argv[1]) == "--projects")
                runProjectPresentationTests();
            else if (std::string_view(argv[1]) == "--visualization")
                runVisualizationPresentationTests();
            else if (std::string_view(argv[1]) == "--runtime")
                runRuntimeIntegrationTests();
            else if (std::string_view(argv[1]) == "--module-panels")
                runModulePanelIntegrationTests();
            else
                throw std::runtime_error("Unknown presentation test selector.");
        });
    return runTests([]
    {
        const juce::ScopedJuceInitialiser_GUI initialise;
        val::application::WorkspaceService service;
        val::presentation::MainComponent shell(service);
        shell.setSize(1000, 700);
        require(shell.getNumChildComponents() == 9,
                "Shell chrome, dock workspace and patch navigation are constructed.");
        require(shell.inspectorVisible(), "Inspector starts visible.");
        shell.setInspectorVisible(false);
        require(!shell.inspectorVisible(), "Inspector can be hidden without a backend.");
        shell.setSize(800, 600);
        shell.setInspectorVisible(true);
        shell.showStatus("Test status");
        juce::Array<juce::CommandID> commands;
        shell.getAllCommands(commands);
        require(commands.size() == 17, "Shell registers history, navigation, layout, project and explicit transport commands.");
        const auto audioMenu = shell.getMenuForIndex(3, "Audio");
        juce::PopupMenu::MenuItemIterator audioItems(audioMenu);
        int transportItems{};
        while (audioItems.next())
        {
            const auto& item = audioItems.getItem();
            require(!item.text.contains("no backend"), "Audio menu contains no obsolete disabled transport placeholders.");
            if (item.text == "Start" || item.text == "Stop") ++transportItems;
        }
        require(transportItems == 2, "Audio menu exposes the real command-bound Start and Stop.");
        auto* nameEditor = findControl<juce::TextEditor>(shell);
        require(nameEditor != nullptr, "Workspace name has an editable control.");
        auto commandId = [&](const juce::String& name)
        {
            for (const auto id : commands)
            {
                juce::ApplicationCommandInfo info(id);
                shell.getCommandInfo(id, info);
                if (info.shortName == name)
                    return id;
            }
            throw std::runtime_error("Expected shell command was not registered.");
        };
        const auto rename = commandId("Rename workspace");
        const auto undo = commandId("Undo");
        const auto redo = commandId("Redo");
        nameEditor->setText("UI test workspace");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(rename))
                && service.workspace().name == "UI test workspace",
                "Presentation command delegates the accepted edit to its service.");
        juce::ApplicationCommandInfo undoInfo(undo);
        shell.getCommandInfo(undo, undoInfo);
        require((undoInfo.flags & juce::ApplicationCommandInfo::isDisabled) == 0,
                "Undo availability reflects application history.");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(undo))
                && service.workspace().name == "Untitled" && nameEditor->getText() == "Untitled",
                "Undo refreshes the UI from application state.");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(redo))
                && service.workspace().name == "UI test workspace",
                "Redo uses the same workspace history.");

        using namespace val::presentation;
        auto& docks = shell.dockWorkspace();
        const auto* originalPanel = &docks.panel(PanelId::projectExplorer);
        for (const auto position : {DockPosition::left, DockPosition::centre,
                                   DockPosition::right, DockPosition::bottom})
        {
            docks.movePanel(PanelId::projectExplorer, position);
            require(docks.layout().placement(PanelId::projectExplorer).position == position,
                    "Moving a panel updates the layout state.");
            require(&docks.panel(PanelId::projectExplorer) == originalPanel
                    && originalPanel->getParentComponent() != nullptr
                    && originalPanel->isVisible(),
                    "Dock changes reparent and select the original view, rather than recreating it.");
        }
        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            docks.setPanelVisible(id, false);
            require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(commandId(panelName(id)))),
                    "Every named area is reachable through a command.");
            require(docks.layout().placement(id).visible && docks.panel(id).isVisible(),
                    "Navigation reopens hidden panels and selects the corresponding tab.");
        }
        require(shell.getMenuBarNames() == juce::StringArray{"File", "Edit", "View", "Audio"},
                "All shell menu groups are discoverable.");
        for (int index = 0; index < shell.getMenuBarNames().size(); ++index)
            require(shell.getMenuForIndex(index, {}).getNumItems() > 0, "Menus contain framework entries.");
        auto& audio = docks.panel(PanelId::audioSettings);
        int unavailableControls{};
        for (auto* child : audio.getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*>(child); combo && !combo->isEnabled())
                ++unavailableControls;
        require(unavailableControls == 0, "Legacy disabled placeholder controls are removed from Audio Settings.");
        require(!shell.runtime().running(), "The shell composes runtime settings without automatically opening a device.");

        const juce::DragAndDropTarget::SourceDetails ownDrag(
            0, &docks.panel(PanelId::patchCanvas), {});
        juce::Component foreign;
        const juce::DragAndDropTarget::SourceDetails foreignDrag(0, &foreign, {});
        require(docks.accepts(ownDrag) && !docks.accepts(foreignDrag),
                "Dock drops only accept this workspace's owned panel instances.");
        docks.movePanel(PanelId::patchCanvas, DockPosition::bottom);
        docks.setPanelVisible(PanelId::moduleBrowser, false);
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(commandId("Reset Layout"))),
                "Reset is available through the command system.");
        const DockLayout defaults;
        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            require(docks.layout().placement(id) == defaults.placement(id),
                    "Command reset restores panel locations and visibility.");
        }
        for (const auto size : {juce::Point<int>{800, 600}, {1100, 720}, {1600, 1000}})
        {
            shell.setSize(size.x, size.y);
            for (auto* child : docks.getChildren())
                if (child->isVisible())
                    require(docks.getLocalBounds().contains(child->getBounds())
                            && !child->getBounds().isEmpty(),
                            "Visible docks and splitters fit supported window sizes.");
            juce::Component* divider{};
            for (auto* child : docks.getChildren())
                if (child->getName().startsWith("Resize bottom dock"))
                    divider = child;
            require(divider != nullptr, "The bottom dock exposes a keyboard-accessible divider.");
            const auto before = docks.panel(PanelId::audioSettings).getParentComponent()->getHeight();
            require(divider->keyPressed(juce::KeyPress(juce::KeyPress::upKey)),
                    "The divider handles its documented arrow key.");
            const auto after = docks.panel(PanelId::audioSettings).getParentComponent()->getHeight();
            require(after == before + 12,
                    "Divider movement is immediate, including when a dock starts at its pixel minimum.");
            for (int step = 0; step < 100; ++step)
                divider->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
            const auto maximum = docks.panel(PanelId::audioSettings).getParentComponent()->getHeight();
            require(maximum <= docks.getHeight() / 2, "Repeated resize commands respect the upper bound.");
            docks.resetLayout();
        }
        const auto snapshot = shell.createComponentSnapshot(shell.getLocalBounds());
        require(snapshot.isValid(), "The complete shell renders without an audio backend.");
        require(service.workspace().name == "UI test workspace" && service.canUndo() && !service.canRedo(),
                "Layout and navigation never add application document commands.");
    });
}
