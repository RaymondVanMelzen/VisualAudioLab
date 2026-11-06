#include "TestSupport.h"
#include <val/presentation/MainComponent.h>
#include <val/presentation/WindowPlacement.h>
#include <array>

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

juce::TextButton* findButton(juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button && (button->getTitle() == name || button->getButtonText() == name))
            return button;
        if (auto* nested = findButton(*child, name))
            return nested;
    }
    return nullptr;
}

juce::Label* findLabel(juce::Component& parent, const juce::String& title)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* label = dynamic_cast<juce::Label*>(child); label && label->getTitle() == title)
            return label;
        if (auto* nested = findLabel(*child, title))
            return nested;
    }
    return nullptr;
}

void clickButton(juce::TextButton& button)
{
    const auto point = button.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
        &button, &button, now, point, now, 1, false};
    juce::Component& target = button;
    target.mouseEnter(event);
    target.mouseDown(event);
    target.mouseUp(event);
}

bool menuContains(const juce::PopupMenu& menu, const juce::String& text)
{
    juce::PopupMenu::MenuItemIterator items(menu);
    while (items.next())
        if (items.getItem().text == text)
            return true;
    return false;
}

void runNodeEditorTests();
void runLargeGraphBenchmark();
void runProjectPresentationTests();
void runVisualizationPresentationTests();
void runRuntimeIntegrationTests();
void runModulePanelIntegrationTests();
void runTypographyTests(const char* directory);

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--typography-evidence")
        return runTests([&]
        {
            const juce::ScopedJuceInitialiser_GUI initialise;
            runTypographyTests(argv[2]);
        });
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
            else if (std::string_view(argv[1]) == "--typography")
                runTypographyTests(nullptr);
            else
                throw std::runtime_error("Unknown presentation test selector.");
        });
    return runTests([]
    {
        const juce::ScopedJuceInitialiser_GUI initialise;
        using namespace val::presentation;
        ShellTheme::setGlowEnabled(true);
        const auto placement = fitWindowToUserArea({-600, -400, 1800, 1200}, {100, 50, 1280, 720});
        require(placement.bounds == juce::Rectangle<int>(100, 50, 1280, 720)
                    && placement.minimumWidth == 640 && placement.minimumHeight == 380
                    && placement.maximumWidth == 1280 && placement.maximumHeight == 720,
                "Oversized and off-screen window bounds fit the logical usable display area.");
        const auto smallDisplayPlacement = fitWindowToUserArea({-50, 20, 1000, 700}, {-1280, 100, 640, 480});
        require(smallDisplayPlacement.bounds == juce::Rectangle<int>(-1280, 100, 640, 480)
                    && smallDisplayPlacement.minimumWidth == 640 && smallDisplayPlacement.minimumHeight == 380,
                "Small-display fitting adapts resize minima to keep the full usable area reachable.");
        const std::array<juce::Rectangle<int>, 4> scaledWorkAreas{{
            {0, 0, 1536, 834}, // 1920x1080 at 125%, less logical taskbar/work-area insets.
            {0, 0, 1280, 693}, // 150%.
            {0, 0, 1097, 577}, // 175%, including a logical taskbar inset.
            {0, 0, 960, 520}   // 200%.
        }};
        for (const auto area : scaledWorkAreas)
        {
            const auto fitted = fitWindowToUserArea({0, 0, 1100, 720}, area);
            require(fitted.bounds.getWidth() <= area.getWidth()
                        && fitted.bounds.getHeight() <= area.getHeight()
                        && fitted.bounds.getX() >= area.getX()
                        && fitted.bounds.getY() >= area.getY()
                        && fitted.bounds.getRight() <= area.getRight()
                        && fitted.bounds.getBottom() <= area.getBottom()
                        && fitted.minimumWidth <= fitted.bounds.getWidth()
                        && fitted.minimumHeight <= fitted.bounds.getHeight(),
                    "Scaled logical work areas, including 1920x1080 at 175%, fit without a 800x600 minimum.");
        }
        const auto highDpi175 = fitWindowToUserArea({0, 0, 1100, 720}, scaledWorkAreas[2]);
        require(highDpi175.bounds == juce::Rectangle<int>(0, 0, 1097, 577)
                    && highDpi175.minimumWidth == 640 && highDpi175.minimumHeight == 380,
                "The 175% work-area regression preserves usable bounds and product-sized resize minima.");
        const juce::BorderSize<int> windowsFrame{32, 8, 8, 8};
        const juce::Rectangle<int> scaledWorkArea{120, 80, 1097, 577};
        const auto framed175 = fitWindowToUserArea({-200, -300, 1100, 720},
                                                   scaledWorkArea, windowsFrame);
        auto framedOuterBounds = framed175.bounds;
        windowsFrame.addTo(framedOuterBounds);
        require(framed175.bounds == juce::Rectangle<int>(128, 112, 1081, 537)
                    && framedOuterBounds == scaledWorkArea
                    && framed175.maximumWidth == scaledWorkArea.getWidth()
                    && framed175.maximumHeight == scaledWorkArea.getHeight(),
                "At simulated 175% geometry, client bounds reserve native titlebar/frame extents and the whole outer window fits.");
        const auto smallerFramed = fitWindowToUserArea({180, 130, 800, 500},
                                                       scaledWorkArea, windowsFrame);
        auto smallerOuterBounds = smallerFramed.bounds;
        windowsFrame.addTo(smallerOuterBounds);
        require(smallerOuterBounds == juce::Rectangle<int>(172, 98, 816, 540),
                "A non-maximized decorated window preserves its client size and remains inside the work area.");
        require(scaledWorkArea.contains(smallerOuterBounds),
                "The full native frame, not only the client area, remains within the usable monitor bounds.");
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* primary = displays.getPrimaryDisplay())
            require(displayForRestoredBounds(displays, {100000, 100000, 800, 600}) == primary,
                    "A saved window on a disconnected monitor recovers to the primary display.");
        val::application::WorkspaceService service;
        MainComponent shell(service);
        shell.setSize(1000, 700);
        require(shell.getNumChildComponents() == 9,
                "Shell chrome, fixed workspace and patch navigation are constructed.");
        require(shell.inspectorVisible(), "Property Inspector starts visible.");
        shell.setInspectorVisible(false);
        require(!shell.inspectorVisible(), "Inspector can collapse without a backend.");
        shell.setSize(800, 600);
        shell.setSize(500, 400);
        require(shell.getChildComponent(1)->getHeight() == 66
                    && shell.getChildComponent(2)->getHeight() == 30
                    && shell.getChildComponent(5)->getY()
                        >= shell.getChildComponent(2)->getBottom(),
                "Compact windows reflow toolbar actions and transport/patch controls into reachable rows.");
        shell.setSize(1097, 577);
        for (int i = 0; i < shell.getNumChildComponents(); ++i)
        {
            const auto bounds = shell.getChildComponent(i)->getBounds();
            require(bounds.getY() >= 0 && bounds.getBottom() <= shell.getHeight(),
                    "Essential shell and workspace components remain inside a 175%-scaled logical work area.");
        }
        const auto* startAtHighDpi = findButton(shell, "Start (F5)");
        require(startAtHighDpi != nullptr && startAtHighDpi->isVisible()
                    && !startAtHighDpi->getLocalBounds().isEmpty(),
                "The transport action remains available at 175%-geometry logical dimensions.");
        shell.setSize(800, 600);
        shell.setInspectorVisible(true);
        shell.showStatus("Test status");

        juce::Array<juce::CommandID> commands;
        shell.getAllCommands(commands);
        require(commands.size() == 17, "Shell registers fixed-panel, project, effects, transport and About commands.");
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
        require(menuContains(shell.getMenuForIndex(3, "Audio"), "Audio Setup"),
                "Audio menu contains the sole Audio Setup entry.");
        require(menuContains(shell.getMenuForIndex(4, "Help"), "About Visual Audio Lab"),
                "Help menu exposes About Visual Audio Lab.");
        require(menuContains(shell.getMenuForIndex(2, "View"), "Reduced effects (no glow)"),
                "View menu exposes a discoverable reduced-effects mode.");
        const auto reducedEffects = commandId("Reduced effects (no glow)");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(reducedEffects))
                    && !ShellTheme::glowEnabled(),
                "Reduced effects disables decorative glow without changing the crisp canvas state.");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(reducedEffects))
                    && ShellTheme::glowEnabled(),
                "Reduced effects can be switched off again.");
        require(!menuContains(shell.getMenuForIndex(2, "View"), "Audio Setup"),
                "View menu does not duplicate Audio Setup.");
        require(findButton(shell, "Audio Setup") == nullptr,
                "Main toolbar and view contain no Audio Setup button.");
        const auto audioMenu = shell.getMenuForIndex(3, "Audio");
        require(menuContains(audioMenu, "Start (F5)") && menuContains(audioMenu, "Stop (Shift+F5)"),
                "Audio menu distinguishes the F5 toggle from the explicit Shift+F5 stop.");
        juce::ApplicationCommandInfo startInfo(commandId("Start (F5)"));
        shell.getCommandInfo(startInfo.commandID, startInfo);
        require((startInfo.flags & juce::ApplicationCommandInfo::isDisabled) == 0,
                "Start remains invokable with an invalid or unfinished device configuration so F5 can explain the issue.");

        auto& canvas = shell.dockWorkspace().canvas();
        const auto dropPoint = canvas.viewport().getCentre();
        const juce::DragAndDropTarget::SourceDetails droppedGain{"Gain", &shell, dropPoint};
        require(canvas.isInterestedInDragSource(droppedGain),
                "The canvas recognizes a real catalog module type as a drag source.");
        canvas.itemDropped(droppedGain);
        require(service.graph().nodes.size() == 1 && service.graph().connections.empty(),
                "Dropping a catalog module creates one node without implicit wiring.");
        const auto& droppedNode = service.graph().nodes.begin()->second;
        const auto expectedPosition = canvas.toPatch(dropPoint.toFloat());
        const val::domain::Position expectedNodePosition{expectedPosition.x, expectedPosition.y};
        require(std::get<std::string>(droppedNode.parameters.at("audio.type")) == "Gain"
                    && droppedNode.position == expectedNodePosition,
                "A dropped module is placed at the canvas pointer location.");
        const juce::DragAndDropTarget::SourceDetails outsideDrop{"Gain", &shell, {0, 0}};
        canvas.itemDropped(outsideDrop);
        auto* status = findLabel(shell, "Application status");
        require(service.graph().nodes.size() == 1 && status && status->getTooltip().contains("nothing was inserted"),
                "Dropping outside the canvas viewport reports rejection and makes no edit.");
        shell.insertModule("Guitar Tuner", dropPoint.toFloat());
        const auto& tunerNode = service.graph().nodes.rbegin()->second;
        require(std::get<std::string>(tunerNode.parameters.at("audio.type")) == "Analyzer"
                    && std::get<std::string>(tunerNode.parameters.at("visualization.kind")) == "Guitar Tuner"
                    && service.graph().connections.empty() && !shell.runtime().running(),
                "The Guitar Tuner is a pass-through analyzer instance with its own view and no implicit wiring or playback.");
        shell.insertModule("Signal Generator", dropPoint.toFloat());
        const auto& signalGenerator = service.graph().nodes.rbegin()->second;
        require(std::get<std::string>(signalGenerator.parameters.at("audio.type")) == "Signal Generator"
                    && std::get<double>(signalGenerator.parameters.at("audio.param.levelDb")) == -6.0,
                "A newly inserted Signal Generator starts at an adjustable -6 dBFS peak target.");
        shell.insertModule("Noise Generator", dropPoint.toFloat());
        const auto& noiseGenerator = service.graph().nodes.rbegin()->second;
        require(std::get<std::string>(noiseGenerator.parameters.at("audio.type")) == "Noise Generator"
                    && std::get<double>(noiseGenerator.parameters.at("audio.param.levelDbRms")) == -6.0
                    && service.graph().connections.empty() && !shell.runtime().running(),
                "A newly inserted Noise Generator uses the peak-reference starting target without wiring or starting audio.");

        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(commandId("Audio Setup"))),
                "Audio Setup command opens its dialog.");
        auto* setupWindow = shell.runtime().findParentComponentOfClass<juce::DocumentWindow>();
        require(setupWindow && setupWindow->getName() == "Audio Setup" && setupWindow->isVisible(),
                "The Audio Setup runtime is hosted in a dedicated dialog.");
        require(!shell.runtime().running(), "Opening setup never starts audio.");
        clickButton(*findButton(shell.runtime(), "Done"));
        require(setupWindow && !setupWindow->isVisible() && !shell.runtime().running(),
                "Closing setup hides the dialog without changing transport (window="
                    + std::string(setupWindow && setupWindow->isVisible() ? "visible" : "hidden")
                    + ", transport=" + (shell.runtime().running() ? "running" : "stopped")
                    + ", status=" + shell.runtime().status().toStdString() + ").");

        auto* nameEditor = findControl<juce::TextEditor>(shell);
        require(nameEditor != nullptr, "Workspace name has an editable control.");
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

        auto& fixed = shell.dockWorkspace();
        auto& browser = fixed.panel(PanelId::moduleBrowser);
        auto* collapseBrowser = findButton(browser, "Collapse Module Browser");
        require(collapseBrowser && collapseBrowser->getTitle() == "Collapse Module Browser"
                && collapseBrowser->getTooltip() == "Collapse Module Browser",
                "Side-panel collapse exposes a descriptive accessible title and help text.");
        clickButton(*collapseBrowser);
        require(!browser.isVisible() && findButton(fixed, "Show Module Browser")->isVisible(),
                "Collapsing a side panel leaves a visible reopen affordance.");
        clickButton(*findButton(fixed, "Show Module Browser"));
        require(browser.isVisible(), "The collapsed panel reopens in its fixed location.");
        fixed.setPanelVisible(PanelId::patchCanvas, false);
        require(fixed.layout().placement(PanelId::patchCanvas).visible
                && fixed.panel(PanelId::patchCanvas).isVisible(),
                "Patch Canvas cannot be collapsed.");

        require(shell.getMenuBarNames() == juce::StringArray{"File", "Edit", "View", "Audio", "Help"},
                "All shell menu groups are discoverable.");
        for (int index = 0; index < shell.getMenuBarNames().size(); ++index)
            require(shell.getMenuForIndex(index, {}).getNumItems() > 0, "Menus contain framework entries.");
        require(shell.perform(juce::ApplicationCommandTarget::InvocationInfo(commandId("Reset Panels"))),
                "Reset Panels is available through the command system.");
        const DockLayout defaults;
        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            require(fixed.layout().placement(id) == defaults.placement(id),
                    "Reset restores fixed panel visibility.");
        }

        for (const auto size : {juce::Point<int>{800, 600}, {1100, 720}, {1600, 1000}})
        {
            shell.setSize(size.x, size.y);
            for (auto* child : fixed.getChildren())
                if (child->isVisible())
                    require(fixed.getLocalBounds().contains(child->getBounds())
                            && !child->getBounds().isEmpty(),
                            "Visible fixed panels, reopen handles and splitters fit supported window sizes.");
            juce::Component* divider{};
            for (auto* child : fixed.getChildren())
                if (child->getName().startsWith("Resize Module Browser width"))
                    divider = child;
            require(divider != nullptr, "The fixed side panel has an accessible width divider.");
            const auto before = fixed.panel(PanelId::moduleBrowser).getWidth();
            require(divider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),
                    "The divider handles its documented arrow key.");
            require(fixed.panel(PanelId::moduleBrowser).getWidth() >= before,
                    "Keyboard resizing updates the fixed panel width.");
            fixed.resetLayout();
        }
        const auto snapshot = shell.createComponentSnapshot(shell.getLocalBounds());
        require(snapshot.isValid(), "The complete fixed shell renders without an audio backend.");
        require(service.workspace().name == "UI test workspace" && service.canUndo() && !service.canRedo(),
                "Panel visibility and layout never add application document commands.");
    });
}
