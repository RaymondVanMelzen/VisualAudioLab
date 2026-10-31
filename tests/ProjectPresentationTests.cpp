#include "TestSupport.h"
#include <val/infrastructure/LocalProjectStore.h>
#include <val/presentation/MainComponent.h>

/** Exercises registered file commands against real storage without automating native pickers. */
void runProjectPresentationTests()
{
    using namespace val;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("val-project-ui-" + juce::Uuid().toString());
    require(directory.createDirectory().wasOk(), "Create UI persistence directory.");
    const auto utf8 = directory.getFullPathName().toStdString();
    const auto path = std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    infrastructure::LocalProjectStore store(path / "recent.json");
    application::WorkspaceService service;
    application::ProjectSession project(service, store);
    {
        presentation::MainComponent shell(service, &project);
        juce::DocumentWindow window("Project commands", juce::Colours::black, juce::DocumentWindow::allButtons);
        window.setContentNonOwned(&shell, false);
        window.setBounds(80, 80, 1100, 720);
        window.setVisible(true);
        juce::Array<juce::CommandID> commands;
        shell.getAllCommands(commands);
        const auto command = [&](const char* name)
        {
            for (auto id : commands)
            {
                juce::ApplicationCommandInfo info(id);
                shell.getCommandInfo(id, info);
                if (info.shortName == name)
                {
                    require((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0,
                            "Project commands are enabled with a storage service.");
                    return id;
                }
            }
            throw std::runtime_error("Missing project command.");
        };
        const auto save = command("Save Project");
        command("Save Project As...");
        command("Load Project...");
        require(service.createNode({10, 20}).succeeded, "Create persistable UI graph.");
        shell.dockWorkspace().canvas().synchronise();
        shell.dockWorkspace().canvas().panBy({120, -40});
        shell.dockWorkspace().movePanel(presentation::PanelId::projectExplorer, presentation::DockPosition::right);
        shell.dockWorkspace().setPanelVisible(presentation::PanelId::audioSettings, false);
        require(shell.saveProjectTo(path / "ui.valab").succeeded, "Save As UI endpoint writes actual project.");
        const auto saved = service.project();
        require(service.createNode({70, 90}).succeeded, "Edit after saving.");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(save)) && !project.dirty(),
                "Registered Save command writes the active path and clears dirty content.");
        require(store.read(path / "ui.valab").file->state.patches.at(1).graph.nodes.size() == 2,
                "Save command reaches the file adapter, not just serialization.");
        require(shell.loadProjectFrom(path / "ui.valab").succeeded && !service.canUndo(),
                "UI load resets old history.");
        require(service.createNode({100, 100}).succeeded, "Prepare graph to replace.");
        shell.dockWorkspace().resetLayout();
        require(shell.loadProjectFrom(path / "ui.valab").succeeded
                && service.graph().nodes.size() == 2
                && shell.dockWorkspace().projectLayout().docks == saved.layout.docks
                && !shell.dockWorkspace().layout().placement(presentation::PanelId::audioSettings).visible,
                "UI load restores graph and dock layout.");
        const auto viewport = shell.dockWorkspace().canvas().navigation();
        require(viewport == saved.patches.at(1).viewport, "UI load restores patch viewport, not selection.");
        shell.getMenuForIndex(0, {});
        require(service.rename("Unsaved replacement").succeeded, "Dirty close fixture.");
        bool closed{};
        shell.dockWorkspace().canvas().grabKeyboardFocus();
        require(window.getPeer()->handleKeyPress(juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0)),
                "The focused canvas routes the standard Save shortcut through its peer and shell mapping.");
        std::string asyncError;
        juce::MessageManager::callAsync([&]
        {
            try
            {
                require(!project.dirty() && store.read(path / "ui.valab").file->state.workspace.name == "Unsaved replacement",
                        "Save shortcut reaches real storage after asynchronous command dispatch.");
                require(service.rename("Unsaved after keyboard save").succeeded, "Prepare another dirty close.");
                shell.requestClose([&] { closed = true; });
                juce::MessageManager::callAsync([&]
                {
                    auto* modal = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
                    if (!modal || closed)
                    {
                        asyncError = "Dirty close did not ask before discarding.";
                        juce::MessageManager::getInstance()->stopDispatchLoop();
                        return;
                    }
                    modal->exitModalState(0);
                    juce::MessageManager::callAsync([&]
                    {
                        if (closed || !project.dirty())
                        {
                            asyncError = "Cancel close lost unsaved content.";
                            juce::MessageManager::getInstance()->stopDispatchLoop();
                            return;
                        }
                        shell.requestClose([&] { closed = true; });
                        juce::MessageManager::callAsync([&]
                        {
                            auto* nextModal = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
                            if (!nextModal)
                            {
                                asyncError = "Second dirty close did not ask again.";
                                juce::MessageManager::getInstance()->stopDispatchLoop();
                                return;
                            }
                            nextModal->exitModalState(1);
                            juce::MessageManager::callAsync([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
                        });
                    });
                });
            }
            catch (const std::exception& error)
            {
                asyncError = error.what();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        });
        juce::MessageManager::getInstance()->runDispatchLoop();
        require(asyncError.empty(), asyncError);
        require(closed && !project.dirty(), "Save-before-close completes only after successful real storage.");
        auto offscreen = *store.read(path / "ui.valab").file;
        offscreen.state.layout.x = 999999;
        offscreen.state.layout.y = -999999;
        require(store.write(path / "offscreen.valab", offscreen, std::nullopt).succeeded, "Prepare offscreen layout.");
        require(shell.loadProjectFrom(path / "offscreen.valab").succeeded, "Load offscreen project into real top-level window.");
        const auto bounds = window.getBounds();
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
        require(display != nullptr && display->userArea.contains(bounds),
                "Offscreen window geometry is recovered into a current monitor work area.");
        window.clearContentComponent();
    }
    require(directory.deleteRecursively(), "Clean UI persistence directory.");
}
