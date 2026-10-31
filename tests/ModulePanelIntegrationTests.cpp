#include "TestSupport.h"
#include <val/audio/AudioGraph.h>
#include <val/presentation/ModulePanels.h>
#include <val/script/Compiler.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace
{
template <typename T>
void collectControls(juce::Component& parent, std::vector<T*>& controls)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* control = dynamic_cast<T*>(child)) controls.push_back(control);
        collectControls(*child, controls);
    }
}

template <typename T>
std::vector<T*> controls(juce::Component& parent)
{
    std::vector<T*> result;
    collectControls(parent, result);
    return result;
}

template <typename T, typename Predicate>
T& control(juce::Component& parent, Predicate predicate)
{
    for (auto* candidate : controls<T>(parent))
        if (predicate(*candidate)) return *candidate;
    throw std::runtime_error("Expected public module-panel control was not found.");
}

juce::TextEditor& textControl(juce::Component& parent, const juce::String& title)
{
    return control<juce::TextEditor>(parent, [&](const auto& text) { return text.getTitle() == title; });
}

juce::TextButton& button(juce::Component& parent, const juce::String& caption)
{
    return control<juce::TextButton>(parent, [&](const auto& item) { return item.getButtonText() == caption; });
}

juce::Slider& slider(juce::Component& parent, const juce::String& title)
{
    return control<juce::Slider>(parent, [&](const auto& item) { return item.getTitle() == title; });
}

juce::MouseEvent sliderPointer(juce::Slider& component, juce::Point<float> point,
                               juce::Point<float> down, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1.0f, 0, 0, 0, 0, &component, &component, now, down, now, 1, dragged};
}

double savedNumber(const val::application::WorkspaceService& service, std::uint64_t id, const char* key)
{
    return std::get<double>(service.graph().nodes.at(id).parameters.at(key));
}

/** Expected host callback record, independent of hardware and processing state. */
struct RuntimeEdit
{
    std::uint64_t node{};
    std::uint16_t parameter{};
    float value{};
    val::audio::ParameterAction action{};
};
}

/**
 * Exercises public JUCE controls and the real document/compiler boundaries, without audio hardware.
 * The caller supplies JUCE GUI initialization on the message thread. This function owns one dispatch
 * loop and stops it on completion, so run it last in a selector that also needs asynchronous GUI work.
 */
void runModulePanelIntegrationTests()
{
    using namespace val;
    using namespace presentation;
    require(juce::MessageManager::getInstance()->isThisTheMessageThread(),
            "Module panel interactions run on the JUCE message thread.");
    require(!juce::MessageManager::getInstance()->hasStopMessageBeenSent(),
            "Run module panel interactions before another test stops the dispatch loop.");

    application::WorkspaceService workspace;
    ModuleCatalog catalog;
    ModuleEditor editor(workspace);
    juce::Component host;
    host.addAndMakeVisible(catalog);
    host.addAndMakeVisible(editor);
    host.setSize(1000, 900);
    catalog.setBounds(0, 0, 280, 900);
    editor.setBounds(280, 0, 720, 900);
    juce::DocumentWindow window("Module panel interactions", juce::Colours::black,
                                 juce::DocumentWindow::allButtons);
    window.setContentNonOwned(&host, false);
    window.setBounds(40, 40, 1000, 900);
    window.setVisible(true);

    std::vector<std::string> inserted;
    bool catalogMatched{true};
    std::vector<RuntimeEdit> runtimeEdits;
    int changed{};
    editor.onChanged = [&] { ++changed; editor.refresh(); };
    editor.onRuntimeParameter = [&](auto id, auto parameter, auto value, auto action)
    {
        runtimeEdits.push_back({id, parameter, value, action});
    };
    std::uint64_t scheduledNode{};
    std::uint16_t scheduledParameter{};
    float scheduledValue{};
    double scheduledSeconds{};
    int scheduled{};
    editor.onScheduleParameter = [&](auto id, auto parameter, auto value, auto seconds)
    {
        ++scheduled;
        scheduledNode = id;
        scheduledParameter = parameter;
        scheduledValue = value;
        scheduledSeconds = seconds;
    };
    catalog.onInsert = [&](std::string type) { inserted.push_back(std::move(type)); };

    auto& search = textControl(catalog, "Search modules");
    auto& list = control<juce::ListBox>(catalog, [](const auto&) { return true; });
    auto& diagnostics = textControl(editor, "Module editor diagnostics");
    std::vector<std::function<void()>> steps;
    const auto add = [&](std::function<void()> step) { steps.push_back(std::move(step)); };
    domain::Graph beforeEffect, afterEffect, beforeScript;
    std::uint64_t eq{}, legacyGain{}, scriptId{};
    double gestureValue{};
    juce::Point<float> gestureStart;
    int changedBefore{};
    std::string authoredSource;
    std::string persistedBefore;

    add([&]
    {
        require(list.getListBoxModel()->getNumRows() == 14 && list.getSelectedRow() >= 0,
                "The actual catalog initially exposes fourteen selectable built-in modules.");
        search.setText("sPeCtRuM");
    });
    add([&]
    {
        require(list.getListBoxModel()->getNumRows() == 1 && list.getSelectedRow() == 0,
                "Search filters the real list case-insensitively and selects its only match.");
        button(catalog, "Insert selected module").triggerClick();
    });
    add([&]
    {
        require(inserted == std::vector<std::string>{"Spectrum Analyzer"},
                "Insertion emits the selected semantic analyzer type, not a generic placeholder.");
        search.setText("not-a-module");
    });
    add([&]
    {
        require(list.getListBoxModel()->getNumRows() == 0 && list.getSelectedRow() == -1
                    && !button(catalog, "Insert selected module").isEnabled(),
                "An unmatched search clears selection and disables insertion.");
        button(catalog, "Insert selected module").triggerClick();
        search.setText("meter");
    });
    add([&]
    {
        require(inserted.size() == 1 && list.getListBoxModel()->getNumRows() == 2,
                "Disabled insertion does not emit a callback; meter search has two real rows.");
        list.selectRow(1);
        list.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    });
    add([&]
    {
        require(inserted.size() == 2 && inserted.back() == "RMS Meter",
                "Keyboard activation inserts the selected RMS row.");
        search.setText({});
    });
    add([&]
    {
        const std::array<std::string, 14> names{
            "Input", "Output", "Gain", "EQ", "Delay", "Compressor", "Distortion",
            "Chorus", "Reverb", "Oscilloscope", "Spectrum Analyzer", "Peak Meter", "RMS Meter", "Script"};
        require(list.getListBoxModel()->getNumRows() == static_cast<int>(names.size()), "Clearing search restores every module.");
        catalog.onInsert = [&, names](std::string type)
        {
            const auto row = list.getSelectedRow();
            catalogMatched = catalogMatched && row >= 0 && row < static_cast<int>(names.size())
                && type == names[static_cast<std::size_t>(row)];
            inserted.push_back(std::move(type));
        };
    });
    for (int row = 0; row < 14; ++row)
        add([&, row] { list.selectRow(row); button(catalog, "Insert selected module").triggerClick(); });
    add([&]
    {
        require(inserted.size() == 16 && catalogMatched,
                "Every actual catalog row can be selected and emits its module type through the insertion button.");
        catalog.onInsert = {};
        require(workspace.createNode({10, 20}, "EQ under test",
                    {{"audio.type", std::string("EQ")}, {"custom.note", std::string("keep me")}}).succeeded,
                "Create a typed effect using the single undoable insertion boundary.");
        eq = workspace.lastCreatedNode();
        editor.setNode(eq);
        require(editor.selected() == eq && editor.node() == eq, "Selection getters expose the inspected identity.");
        audio::EqNode processor;
        const auto specifications = processor.parameters();
        require(controls<juce::Slider>(editor).size() == specifications.size(),
                "Every actual effect parameter has an editable slider.");
        for (const auto& specification : specifications)
        {
            const auto& item = slider(editor, juce::String(specification.name) + " saved base value");
            require(item.getMinimum() == specification.minimum && item.getMaximum() == specification.maximum
                        && std::abs(item.getValue() - specification.initial) < 0.000001,
                    "Editor ranges and initial values are derived from the processor metadata.");
        }
        beforeEffect = workspace.graph();
        changedBefore = changed;
        auto& frequency = slider(editor, "frequency saved base value");
        gestureStart = {static_cast<float>(frequency.getPositionOfValue(frequency.getValue())),
                        static_cast<float>(frequency.getHeight()) * 0.5f};
        frequency.mouseDown(sliderPointer(frequency, gestureStart, gestureStart, false));
        const juce::Point<float> target{static_cast<float>(frequency.getPositionOfValue(5000)),
                                       gestureStart.y};
        frequency.mouseDrag(sliderPointer(frequency, target, gestureStart, true));
        gestureValue = frequency.getValue();
        require(std::abs(gestureValue - 1000) > 100 && workspace.graph() == beforeEffect && changed == changedBefore,
                "Pointer dragging visibly changes the slider but does not mutate document/history mid-gesture.");
    });
    add([&]
    {
        require(workspace.graph() == beforeEffect && changed == changedBefore,
                "Queued slider notifications still do not commit an in-progress gesture.");
        auto& frequency = slider(editor, "frequency saved base value");
        const juce::Point<float> target{static_cast<float>(frequency.getPositionOfValue(gestureValue)), gestureStart.y};
        frequency.mouseUp(sliderPointer(frequency, target, gestureStart, true));
    });
    add([&]
    {
        require(std::abs(savedNumber(workspace, eq, "audio.param.frequency") - gestureValue) < 0.001
                    && changed == changedBefore + 1 && runtimeEdits.empty(),
                "Gesture completion persists one base edit, notifies the host once, and emits no transient event.");
        require(std::get<std::string>(workspace.graph().nodes.at(eq).parameters.at("custom.note")) == "keep me"
                    && workspace.graph().nodes.at(eq).position == domain::Position{10, 20},
                "Editing one parameter preserves unrelated state and node geometry.");
        afterEffect = workspace.graph();
        require(workspace.undo().succeeded && workspace.graph() == beforeEffect,
                "A single undo reverts the entire slider gesture.");
        editor.refresh();
        require(slider(editor, "frequency saved base value").getValue() == 1000,
                "Refreshing after undo restores the displayed base.");
        require(workspace.redo().succeeded && workspace.graph() == afterEffect,
                "Redo restores the single committed gesture.");
        editor.refresh();
        control<juce::ToggleButton>(editor, [](const auto&) { return true; }).triggerClick();
    });
    add([&]
    {
        require(std::get<bool>(workspace.graph().nodes.at(eq).parameters.at("audio.bypass"))
                    && changed == changedBefore + 2 && runtimeEdits.empty(),
                "The real bypass button persists and notifies without emitting a transient override.");
        require(workspace.undo().succeeded && workspace.graph() == afterEffect,
                "Bypass has its own existing undo-history entry.");
        editor.refresh();
        require(!control<juce::ToggleButton>(editor, [](const auto&) { return true; }).getToggleState(),
                "Undo refresh restores bypass state.");
        auto& frequency = slider(editor, "frequency saved base value");
        frequency.showTextBox();
        auto& value = control<juce::TextEditor>(frequency, [](const auto&) { return true; });
        value.setText("2300");
        value.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    });
    add([&]
    {
        require(std::abs(savedNumber(workspace, eq, "audio.param.frequency") - 2300) < 0.001,
                "Typing a real slider value and pressing Enter commits a numeric base.");
        textControl(editor, "frequency live override or automation target").setText("2700", false);
        button(editor, "Apply override").triggerClick();
    });
    add([&]
    {
        require(runtimeEdits.size() == 1 && runtimeEdits.back().node == eq && runtimeEdits.back().parameter == 0
                    && runtimeEdits.back().value == 2700
                    && runtimeEdits.back().action == audio::ParameterAction::overrideValue
                    && savedNumber(workspace, eq, "audio.param.frequency") == 2300,
                "Override uses the immutable processor parameter index without replacing the saved base.");
        button(editor, "Release override").triggerClick();
    });
    add([&]
    {
        require(runtimeEdits.back().action == audio::ParameterAction::releaseOverride,
                "Release override exposes the existing runtime layer action.");
        button(editor, "Automation now").triggerClick();
    });
    add([&]
    {
        require(runtimeEdits.back().action == audio::ParameterAction::automation
                    && runtimeEdits.back().value == 2700,
                "Automation now sends the authored target to the runtime layer.");
        button(editor, "Disable automation").triggerClick();
    });
    add([&]
    {
        require(runtimeEdits.back().action == audio::ParameterAction::disableAutomation,
                "Disable automation exposes the existing runtime layer action.");
        textControl(editor, "Seconds ahead for scheduled automation target").setText("0.25", false);
        button(editor, "Schedule automation target").triggerClick();
    });
    add([&]
    {
        require(scheduled == 1 && scheduledNode == eq && scheduledParameter == 0
                    && scheduledValue == 2700 && scheduledSeconds == 0.25,
                "Scheduling passes node, parameter index, target and relative seconds to the host.");
        textControl(editor, "Seconds ahead for scheduled automation target").setText("-1", false);
        button(editor, "Schedule automation target").triggerClick();
    });
    add([&]
    {
        require(scheduled == 1 && diagnostics.getText().contains("nonnegative"),
                "Invalid schedule delays are rejected visibly without calling the host.");
        textControl(editor, "frequency live override or automation target").setText("not a number", false);
        button(editor, "Apply override").triggerClick();
    });
    add([&]
    {
        require(runtimeEdits.size() == 4 && diagnostics.getText().contains("finite"),
                "Malformed live targets are rejected visibly without a fake runtime event.");
        require(workspace.createNode({70, 80}, "Legacy Gain",
                    {{"audio.type", std::string("Gain")}, {"audio.gain", 1.5},
                     {"custom.note", std::string("legacy state")}}).succeeded,
                "Create a legacy gain fixture.");
        legacyGain = workspace.lastCreatedNode();
        editor.setNode(legacyGain);
        slider(editor, "gain saved base value").setValue(2.25, juce::sendNotificationSync);
    });
    add([&]
    {
        const auto& node = workspace.graph().nodes.at(legacyGain);
        require(savedNumber(workspace, legacyGain, "audio.gain") == 2.25
                    && !node.parameters.contains("audio.param.gain")
                    && std::get<std::string>(node.parameters.at("custom.note")) == "legacy state",
                "Legacy gain edits retain audio.gain and never create the conflicting modern key.");
        require(audio::AudioGraph::fromStructural(workspace.graph(), 1).value.has_value(),
                "Edited legacy gain still maps through the actual audio graph API.");
        require(workspace.undo().succeeded && savedNumber(workspace, legacyGain, "audio.gain") == 1.5,
                "Legacy gain edits use existing undo history.");
        require(workspace.createNode({90, 100}, "Script under test",
                    {{"audio.type", std::string("Script")}, {"audio.script", ModuleEditor::defaultScriptSource()},
                     {"custom.note", std::string("script state")}}).succeeded,
                "Create a safe mono script fixture.");
        scriptId = workspace.lastCreatedNode();
        editor.setNode(scriptId);
        const auto initial = script::Compiler::compile(ModuleEditor::defaultScriptSource());
        require(initial.value && (*initial.value)->inputs().size() == 1 && (*initial.value)->outputs().size() == 1
                    && controls<juce::Slider>(editor).empty(),
                "The default is a verified mono pass-through with no invented parameters.");
        const auto& draftLabel = control<juce::Label>(editor, [](const auto& label)
        {
            return label.getText().contains("not saved until Compile / Apply");
        });
        require(draftLabel.isVisible(), "Script drafts have a persistent visible warning independent of diagnostics.");
        beforeScript = workspace.graph();
        changedBefore = changed;
        textControl(editor, "VAL Script source draft").setText(
            "NODE Broken AT AUDIO;\nOUTPUT OutSample: REAL;\nBEGIN\n OutSample := Missing\nEND.", false);
        button(editor, "Compile / Apply").triggerClick();
    });
    add([&]
    {
        require(workspace.graph() == beforeScript && changed == changedBefore
                    && diagnostics.getText().contains("Line 4, column"),
                "Failed compilation displays an authored source location and preserves source/history.");
        authoredSource =
            "NODE Authored AT AUDIO;\nINPUT InSample: REAL;\nOUTPUT OutSample: REAL;\n"
            "PARAM Amount: REAL = 0.75 RANGE 0.0 .. 1.0;\nBEGIN\n"
            " OutSample := InSample * Amount\nEND.\n";
        textControl(editor, "VAL Script source draft").setText(juce::String(authoredSource), false);
        require(workspace.graph() == beforeScript, "Editing a source draft alone does not save it.");
        editor.setNode(scriptId);
        editor.refresh();
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == authoredSource,
                "Repeated same-ID selection and refresh preserve unsaved authored source.");
        editor.setNode(eq);
        editor.setNode(scriptId);
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == authoredSource
                    && diagnostics.getText().contains("draft preserved"),
                "Switching selection preserves a node-local draft and visibly labels it unsaved.");
        const auto externalSource = ModuleEditor::defaultScriptSource() + "\n// external saved-source edit\n";
        require(workspace.setParameter(scriptId, "audio.script", externalSource).succeeded,
                "Prepare an external saved-source edit while an authored draft remains open.");
        editor.refresh();
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == authoredSource
                    && diagnostics.getText().contains("Saved source changed"),
                "External saved-source changes preserve the draft and visibly identify the conflict.");
        button(editor, "Revert to saved source").triggerClick();
    });
    add([&]
    {
        const auto saved = std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script"));
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == saved
                    && diagnostics.getText().contains("Draft discarded") && changed == changedBefore,
                "Visible revert explicitly discards the stale draft and restores current saved source without editing history.");
        editor.setNode(eq);
        editor.setNode(scriptId);
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == saved,
                "A reverted draft is not resurrected by later selection changes.");
        textControl(editor, "VAL Script source draft").setText(juce::String(authoredSource), false);
        require(workspace.undo().succeeded && workspace.graph() == beforeScript,
                "Undo the external edit without touching the unsaved draft.");
        editor.refresh();
        require(textControl(editor, "VAL Script source draft").getText().toStdString() == authoredSource,
                "Undo-triggered refresh retains the unsaved authored draft.");
        button(editor, "Compile / Apply").triggerClick();
    });
    add([&]
    {
        require(changed == changedBefore + 1
                    && std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script")) == authoredSource,
                "Compile / Apply persists exactly the successfully compiled authored text.");
        const auto& parameter = slider(editor, "AMOUNT saved base value");
        require(parameter.getMinimum() == 0 && parameter.getMaximum() == 1 && parameter.getValue() == 0.75
                    && controls<juce::Slider>(editor).size() == 1,
                "Script parameter controls come from the verified authored program.");
        require(std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("custom.note")) == "script state",
                "Applying source preserves unrelated node state.");
        require(workspace.undo().succeeded && workspace.graph() == beforeScript,
                "One undo restores the previous script source and all document state.");
        editor.refresh();
        require(controls<juce::Slider>(editor).empty(), "Undo refresh removes parameters absent from restored source.");
        require(workspace.redo().succeeded, "Redo the authored source application.");
        editor.refresh();
        slider(editor, "AMOUNT saved base value").setValue(0.5, juce::sendNotificationSync);
    });
    add([&]
    {
        require(savedNumber(workspace, scriptId, "audio.param.AMOUNT") == 0.5,
                "Verified script parameters persist through the same real slider path.");
        persistedBefore = std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script"));
        control<juce::ComboBox>(editor, [](const auto&) { return true; })
            .setSelectedId(1, juce::sendNotificationSync);
        button(editor, "Compile / Apply").triggerClick();
    });
    add([&]
    {
        require(std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script")) == persistedBefore
                    && savedNumber(workspace, scriptId, "audio.param.AMOUNT") == 0.5
                    && diagnostics.getText().contains("not applied"),
                "A new valid source incompatible with saved parameters is visibly rejected without losing state.");
    });
    for (int example = 1; example <= 4; ++example)
    {
        add([&, example]
        {
            require(workspace.createNode({0, 0}, "Example",
                        {{"audio.type", std::string("Script")},
                         {"audio.script", ModuleEditor::defaultScriptSource()}}).succeeded,
                    "Create independent source fixture for an embedded example.");
            scriptId = workspace.lastCreatedNode();
            editor.setNode(scriptId);
            persistedBefore = std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script"));
            changedBefore = changed;
            auto& examples = control<juce::ComboBox>(editor, [](const auto&) { return true; });
            require(examples.getNumItems() == 4, "All embedded runnable examples are selectable.");
            examples.setSelectedId(example, juce::sendNotificationSync);
            const auto source = textControl(editor, "VAL Script source draft").getText().toStdString();
            require(script::Compiler::compile(source).value.has_value(), "The selected embedded source actually compiles.");
            require(std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script")) == persistedBefore
                        && changed == changedBefore,
                    "Selecting an example changes only the draft.");
            authoredSource = source;
            button(editor, "Compile / Apply").triggerClick();
        });
        add([&, example]
        {
            require(changed == changedBefore + 1
                        && std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script")) == authoredSource,
                    "Each selected embedded example applies its actual verified text.");
            const auto compiled = script::Compiler::compile(authoredSource);
            require(compiled.value.has_value(), "Saved example source remains compiler-verifiable.");
            const auto program = *compiled.value;
            const auto expectedChannels = example == 4 ? std::size_t{2} : std::size_t{1};
            require(program->outputs().size() == expectedChannels
                        && controls<juce::Slider>(editor).size() == program->parameters().size(),
                    "Example bus declarations and parameter controls match the saved compiled artifact.");
            for (const auto& definition : program->parameters())
            {
                const auto& value = slider(editor, juce::String(definition.name) + " saved base value");
                require(value.getMinimum() == definition.minimum && value.getMaximum() == definition.maximum
                            && value.getValue() == definition.initial,
                        "Example parameter controls use verified ranges and defaults.");
            }
            std::vector<script::Value> scratch(program->slots());
            for (const auto& input : program->inputs()) scratch[input.slot] = script::Value::fromReal(0.25f);
            for (const auto& parameter : program->parameters())
                scratch[parameter.slot] = script::Value::fromReal(parameter.initial);
            require(program->execute(scratch).code == script::FaultCode::none,
                    "Selected example executes its verified instructions, without hardware.");
            for (const auto& output : program->outputs())
                require(std::isfinite(scratch[output.slot].real), "Every example produces finite output.");
        });
    }
    add([&]
    {
        persistedBefore = std::get<std::string>(workspace.graph().nodes.at(scriptId).parameters.at("audio.script"));
        textControl(editor, "VAL Script source draft").setText(
            juce::String(ModuleEditor::defaultScriptSource() + "\n// unapplied previous-project draft\n"), false);
        auto loaded = workspace.project();
        loaded.patches.at(loaded.activePatch).graph.nodes.at(scriptId).parameters["audio.script"] =
            ModuleEditor::defaultScriptSource() + "\n// source in loaded project\n";
        require(workspace.replaceProject(std::move(loaded)).succeeded, "Simulate project Load reusing patch/node identities.");
        editor.setNode(scriptId);
        require(textControl(editor, "VAL Script source draft").getText().contains("previous-project draft")
                    && diagnostics.getText().contains("Saved source changed"),
                "Project replacement preserves the unapplied draft and visibly labels its saved-source conflict.");
        button(editor, "Revert to saved source").triggerClick();
    });
    add([&]
    {
        require(textControl(editor, "VAL Script source draft").getText().contains("source in loaded project")
                    && !textControl(editor, "VAL Script source draft").getText().contains("previous-project draft")
                    && !workspace.canUndo(),
                "Revert restores the loaded saved source without creating history or applying the previous project's draft.");
        require(workspace.createNode({0, 0}, "Gain").succeeded, "Create a generic node with a misleading display name.");
        editor.setNode(workspace.lastCreatedNode());
        require(controls<juce::Slider>(editor).empty() && diagnostics.getText().contains("explicit audio.type"),
                "Display names never grant DSP capability to a generic node.");
        editor.setNode(999999);
        require(controls<juce::Slider>(editor).empty() && diagnostics.getText().contains("no longer exists"),
                "Absent selection is rejected visibly.");
        editor.setNode(0);
        require(diagnostics.getText().contains("Select an audio module"), "Empty selection has a clear editor state.");
        editor.setStatusMessage("Host scheduling admission failure");
        require(diagnostics.getText() == "Host scheduling admission failure", "Host runtime errors are visible in the editor.");
    });

    std::size_t next{};
    std::string failure;
    const auto alive = std::make_shared<bool>(true);
    juce::Timer::callAfterDelay(15000, [alive]
    {
        if (*alive) juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    std::function<void()> advance;
    advance = [&]
    {
        try
        {
            if (next == steps.size())
            {
                juce::MessageManager::getInstance()->stopDispatchLoop();
                return;
            }
            steps[next++]();
            // Text edits, button activation and slider notifications use the real message queue.
            juce::MessageManager::callAsync([&]
            {
                juce::MessageManager::callAsync([&] { advance(); });
            });
        }
        catch (const std::exception& error)
        {
            failure = "Module panel step " + std::to_string(next) + ": " + error.what();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    };
    juce::MessageManager::callAsync([&] { advance(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    *alive = false;
    window.clearContentComponent();
    require(failure.empty(), failure);
    require(next == steps.size(), "Module panel interaction tests completed before their dispatch-loop watchdog.");
}
