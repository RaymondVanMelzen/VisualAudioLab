#include "TestSupport.h"
#include <val/presentation/MainComponent.h>

int main()
{
    return runTests([]
    {
        const juce::ScopedJuceInitialiser_GUI initialise;
        val::application::WorkspaceService service;
        val::presentation::MainComponent shell(service);
        shell.setSize(1000, 700);
        require(shell.getNumChildComponents() > 5, "Named shell controls are constructed.");
        require(shell.inspectorVisible(), "Inspector starts visible.");
        shell.setInspectorVisible(false);
        require(!shell.inspectorVisible(), "Inspector can be hidden without a backend.");
        shell.setSize(800, 600);
        shell.setInspectorVisible(true);
        shell.showStatus("Test status");
        juce::Array<juce::CommandID> commands;
        shell.getAllCommands(commands);
        require(commands.size() == 5, "Shell registers its structural commands.");
        juce::TextEditor* nameEditor{};
        for (auto* child : shell.getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
                nameEditor = editor;
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
    });
}
