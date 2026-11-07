#include "TestSupport.h"
#include <val/audio/AudioGraph.h>
#include <val/presentation/ModulePanels.h>
#include <val/presentation/ShellTheme.h>
#include <algorithm>
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

template <typename T>
std::vector<T*> visibleControls(juce::Component& parent)
{
    auto result = controls<T>(parent);
    std::erase_if(result, [](const auto* control) { return !control->isVisible(); });
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

void saveScreenshotIfRequested(juce::Component& component, const char* variable)
{
    const auto path = juce::SystemStats::getEnvironmentVariable(variable, {});
    if (path.isEmpty()) return;
    const auto file = juce::File(path);
    auto stream = file.createOutputStream();
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f,
                                                         juce::SoftwareImageType{});
    require(stream && stream->setPosition(0) && stream->truncate().wasOk() && image.isValid()
                && juce::PNGImageFormat{}.writeImageToStream(image, *stream),
            "Save the actual generator editor screenshot.");
    stream->flush();
    require(stream->getStatus().wasOk(), "Generator editor screenshot reaches storage.");
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
    ShellTheme theme;
    ModuleCatalog catalog;
    ModuleEditor editor(workspace);
    juce::Component host;
    host.setLookAndFeel(&theme);
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
    editor.onReadGeneratorMeters = [](std::uint64_t)
    { return GeneratorMeterReadout{"Live", -12.0f, -15.01f, true, true}; };
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
    auto& category = control<juce::ComboBox>(catalog,
        [](const auto& item) { return item.getTitle() == "Module category"; });
    auto& diagnostics = textControl(editor, "Module editor diagnostics");
    std::vector<std::function<void()>> steps;
    const auto add = [&](std::function<void()> step) { steps.push_back(std::move(step)); };
    domain::Graph beforeEffect, afterEffect;
    std::uint64_t eq{}, legacyGain{}, retiredNode{}, generator{};
    int controlChangedBefore{};
    double gestureValue{};
    juce::Point<float> gestureStart;
    int changedBefore{};

    add([&]
    {
        require(list.getListBoxModel()->getNumRows() == 21 && list.getSelectedRow() >= 0
                    && category.getNumItems() == 5,
                "The actual catalog exposes the delivered modules and their populated categories.");
        category.setSelectedId(2, juce::sendNotificationSync);
        require(list.getListBoxModel()->getNumRows() == 7,
                "Effects category contains the seven implemented effects.");
        category.setSelectedId(4, juce::sendNotificationSync);
        require(list.getListBoxModel()->getNumRows() == 6,
                "Analysis contains five measurement tools and the guitar tuner.");
        category.setSelectedId(3, juce::sendNotificationSync);
        require(list.getListBoxModel()->getNumRows() == 7,
                "Generators category exposes the seven separately insertable signal sources.");
        category.setSelectedId(5, juce::sendNotificationSync);
        require(list.getListBoxModel()->getNumRows() == 1
                    && list.getListBoxModel()->getTooltipForRow(0).contains("Explicit unity sum"),
                "Routing utilities keep explicit Sum; hardware endpoints are configured in fixed canvas lanes.");
        category.setSelectedId(1, juce::sendNotificationSync);
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
        require(inserted.size() == 1 && list.getListBoxModel()->getNumRows() == 3,
                "Disabled insertion does not emit a callback; meter search exposes peak, RMS and correlation rows.");
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
        const std::array<std::string, 21> names{
            "Sum", "Gain", "EQ", "Delay", "Compressor", "Distortion",
            "Chorus", "Reverb", "Signal Generator", "Impulse Generator", "Click Generator", "Burst Generator",
            "Chirp Generator", "Noise Generator", "DC Generator", "Oscilloscope", "Spectrum Analyzer",
            "Peak Meter", "RMS Meter", "Correlation Meter", "Guitar Tuner"};
        require(list.getListBoxModel()->getNumRows() == static_cast<int>(names.size()), "Clearing search restores every module.");
        catalog.onInsert = [&, names](std::string type)
        {
            const auto row = list.getSelectedRow();
            catalogMatched = catalogMatched && row >= 0 && row < static_cast<int>(names.size())
                && type == names[static_cast<std::size_t>(row)];
            inserted.push_back(std::move(type));
        };
    });
    for (int row = 0; row < 21; ++row)
        add([&, row] { list.selectRow(row); button(catalog, "Insert selected module").triggerClick(); });
    add([&]
    {
        require(inserted.size() == 23 && catalogMatched,
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
        const auto allSliders = controls<juce::Slider>(editor);
        const auto visibleSliders = std::count_if(allSliders.begin(), allSliders.end(),
            [](const auto* item) { return item->isVisible(); });
        require(static_cast<std::size_t>(visibleSliders) == specifications.size(),
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
        const std::string legacySource = "NODE Old AT AUDIO; OUTPUT Value: REAL; BEGIN Value := 1 END.";
        require(workspace.createNode({90, 100}, "Retired VAL Script",
                    {{"audio.type", std::string("Script")}, {"audio.script", legacySource},
                     {"audio.param.Gain", 0.75}, {"custom.note", std::string("script state")}}).succeeded,
                "Load a retired Script node without compiling or rewriting its saved source.");
        retiredNode = workspace.lastCreatedNode();
        editor.setNode(retiredNode);
        require(diagnostics.getText().contains("Retired VAL Script")
                    && visibleControls<juce::Slider>(editor).empty()
                    && std::get<std::string>(workspace.graph().nodes.at(retiredNode).parameters.at("audio.script")) == legacySource
                    && std::get<std::string>(workspace.graph().nodes.at(retiredNode).parameters.at("custom.note")) == "script state",
                "The inspector clearly diagnoses legacy Script as unsupported while retaining its source and unrelated data.");
        require(workspace.undo().succeeded && !workspace.graph().nodes.contains(retiredNode),
                "The legacy diagnostic fixture is removed before validating runnable graph behavior.");
    });
    add([&]
    {
        require(workspace.createNode({0, 0}, "Signal Generator",
                    {{"audio.type", std::string("Signal Generator")},
                     {"audio.param.levelDb", -12.0}}).succeeded,
                "Create a zero-input source for end-to-end typed control editor tests.");
        generator = workspace.lastCreatedNode();
        editor.setNode(generator);
        saveScreenshotIfRequested(editor, "VAL_TOOLKIT_EDITOR_SCREENSHOT");
        const auto editorBounds = editor.getBounds();
        editor.setBounds(editorBounds.getX(), editorBounds.getY(), 360, 600);
        saveScreenshotIfRequested(editor, "VAL_TOOLKIT_EDITOR_COMPACT_SCREENSHOT");
        editor.setBounds(editorBounds);
        auto& goal = control<juce::ComboBox>(editor, [](const auto& item) { return item.getTitle() == "Signal goal"; });
        auto& goalLabel = control<juce::Label>(editor, [](const auto& item)
            { return item.getText() == "Start with a goal"; });
        auto& peakReadout = control<juce::Label>(editor, [](const auto& item)
            { return item.getTitle() == "Measured sample peak in dBFS over the latest 50 milliseconds"; });
        require(goal.getText() == "Fixed tone" && goal.getNumItems() == 5
                    && slider(editor, "Output Level target peak in dBFS").isVisible()
                    && goalLabel.getFont().getHeight() >= 16
                    && peakReadout.getFont().getHeight() >= 18
                    && control<juce::Component>(editor, [](const auto& item)
                        { return item.getName() == "Generator waveform and sweep preview"; }).isVisible(),
                "The generator opens on a readable task layout with five goals, target level and a static signal preview.");
        require(peakReadout.getText().contains("-12.0 dBFS")
                    && control<juce::Label>(editor, [](const auto& item)
                    { return item.getTitle() == "Measured RMS in dBFS over a rolling 300 millisecond window"; })
                    .getText().contains("-15.0 dBFS"),
                "The task editor distinguishes measured sample peak and RMS readouts.");
        auto& mute = control<juce::ToggleButton>(editor,
            [](const auto& item) { return item.getTitle() == "Mute generator output without stopping transport"; });
        require(mute.isVisible() && !mute.getToggleState(),
                "The task editor exposes a clear, initially unmuted output control.");
        mute.setToggleState(true, juce::dontSendNotification);
        mute.onClick();
        require(savedNumber(workspace, generator, "audio.param.muted") == 1,
                "Mute output persists through the generator's validated parameter path.");
        mute.setToggleState(false, juce::dontSendNotification);
        mute.onClick();
        goal.setSelectedId(2, juce::sendNotificationSync);
        require(savedNumber(workspace, generator, "audio.param.mode") == 1
                    && slider(editor, "Sweep start frequency in hertz").isVisible()
                    && slider(editor, "Sweep end frequency in hertz").isVisible(),
                "Choosing Frequency Sweep exposes its distinct start/end controls and persists the requested mode.");
        goal.setSelectedId(1, juce::sendNotificationSync);
        goal.setSelectedId(4, juce::sendNotificationSync);
        auto& noiseColor = control<juce::ComboBox>(editor, [](const auto& item) { return item.getTitle() == "Noise color"; });
        noiseColor.setSelectedId(2, juce::sendNotificationSync);
        require(savedNumber(workspace, generator, "audio.param.waveform") == 5,
                "The Noise goal exposes White/Pink choices without replacing the existing waveform DSP mode.");
        goal.setSelectedId(1, juce::sendNotificationSync);
        auto& automationDisclosure = control<juce::TextButton>(editor, [](const auto& item)
            { return item.getTitle() == "Show or hide automation controls"; });
        require(automationDisclosure.isEnabled() && automationDisclosure.isVisible(),
                "Automation disclosure is available to the user.");
        automationDisclosure.setToggleState(true, juce::dontSendNotification);
        automationDisclosure.onClick();
        auto& controlTarget = control<juce::ComboBox>(editor,
            [](const auto& item) { return item.getTitle() == "Continuous parameter receiving a scalar control mapping"; });
        require(controlTarget.isVisible(), "Automation disclosure reveals the control-mapping editor.");
        auto& controlSource = control<juce::ComboBox>(editor,
            [](const auto& item) { return item.getTitle() == "Typed scalar source for the selected parameter"; });
        auto selectTarget = [&](const juce::String& name)
        {
            for (int item = 0; item < controlTarget.getNumItems(); ++item)
                if (controlTarget.getItemText(item) == name)
                    controlTarget.setSelectedId(controlTarget.getItemId(item), juce::sendNotificationSync);
            require(controlTarget.getText() == name, "Select a supported continuous generator target.");
        };
        selectTarget("frequencyHz");
        controlSource.setSelectedId(2, juce::sendNotificationSync);
        textControl(editor, "Control mapping offset in destination parameter units").setText("0", false);
        textControl(editor, "Control mapping depth in destination parameter units").setText("20", false);
        textControl(editor, "LFO rate in hertz").setText("2", false);
        auto& controlSave = button(editor, "Save typed control mapping");
        require(controlSource.getSelectedId() == 2 && controlSave.isVisible() && controlSave.isEnabled(),
                "LFO mapping editor is active and save action is enabled.");
        controlChangedBefore = changed;
        controlSave.triggerClick();
    });
    add([&]
    {
        require(changed == controlChangedBefore + 1,
                "Saving the mapping triggers one undoable document edit: " + diagnostics.getText().toStdString());
        const auto lfoSaved = workspace.graph().nodes.at(generator).parameters.find("audio.control.frequencyHz.source");
        std::string savedKeys;
        for (const auto& [key, value] : workspace.graph().nodes.at(generator).parameters)
        {
            (void) value;
            savedKeys += key + "; ";
        }
        require(lfoSaved != workspace.graph().nodes.at(generator).parameters.end(),
                "LFO control source field is saved: " + diagnostics.getText().toStdString() + " keys=" + savedKeys);
        require(std::get<std::string>(lfoSaved->second) == "lfo",
                "LFO selection is persisted exactly.");
        const auto lfoGraph = audio::AudioGraph::fromStructural(workspace.graph(), 1);
        require(lfoGraph.value.has_value(),
                "LFO-to-frequency mapping is persisted, graph-validated and visible through the real editor: "
                    + lfoGraph.error.message);
    });
    add([&]
    {
        auto& controlTarget = control<juce::ComboBox>(editor,
            [](const auto& item) { return item.getTitle() == "Continuous parameter receiving a scalar control mapping"; });
        auto& controlSource = control<juce::ComboBox>(editor,
            [](const auto& item) { return item.getTitle() == "Typed scalar source for the selected parameter"; });

        for (int item = 0; item < controlTarget.getNumItems(); ++item)
            if (controlTarget.getItemText(item) == "levelDb")
                controlTarget.setSelectedId(controlTarget.getItemId(item), juce::sendNotificationSync);
        controlSource.setSelectedId(3, juce::sendNotificationSync);
        textControl(editor, "Control mapping offset in destination parameter units").setText("0", false);
        textControl(editor, "Control mapping depth in destination parameter units").setText("12", false);
        textControl(editor, "Envelope attack duration in seconds").setText("0.1", false);
        textControl(editor, "Envelope hold duration in seconds").setText("0.2", false);
        textControl(editor, "Envelope release duration in seconds").setText("0.5", false);
        button(editor, "Save typed control mapping").triggerClick();
    });
    add([&]
    {
        auto& controlTarget = control<juce::ComboBox>(editor,
            [](const auto& item) { return item.getTitle() == "Continuous parameter receiving a scalar control mapping"; });
        for (int item = 0; item < controlTarget.getNumItems(); ++item)
            if (controlTarget.getItemText(item) == "levelDb")
                controlTarget.setSelectedId(controlTarget.getItemId(item), juce::sendNotificationSync);
        const auto envelopeSaved = workspace.graph().nodes.at(generator).parameters.find("audio.control.levelDb.source");
        require(envelopeSaved != workspace.graph().nodes.at(generator).parameters.end()
                    && std::get<std::string>(envelopeSaved->second) == "envelope"
                    && audio::AudioGraph::fromStructural(workspace.graph(), 1).value,
                "One-shot Envelope-to-level mapping is persisted and graph-validated from the real editor: "
                    + diagnostics.getText().toStdString());
        require(workspace.createNode({0, 0}, "Gain").succeeded, "Create a generic node with a misleading display name.");
        editor.setNode(workspace.lastCreatedNode());
        require(visibleControls<juce::Slider>(editor).empty() && diagnostics.getText().contains("explicit audio.type"),
                "Display names never grant DSP capability to a generic node.");
        editor.setNode(999999);
        require(visibleControls<juce::Slider>(editor).empty() && diagnostics.getText().contains("no longer exists"),
                "Absent selection is rejected visibly.");
        editor.setNode(0);
        require(diagnostics.getText().contains("Select an audio module"), "Empty selection has a clear editor state.");
        require(workspace.createNode({0, 0}, "Noise under test",
                    {{"audio.type", std::string("Noise Generator")}}).succeeded,
                "Create a standalone Noise Generator for legacy level-key coverage.");
        const auto noiseId = workspace.lastCreatedNode();
        editor.setNode(noiseId);
        require(control<juce::ComboBox>(editor, [](const auto& item) { return item.getTitle() == "Noise color"; }).isVisible()
                    && slider(editor, "Output Level target peak in dBFS").isVisible(),
                "Noise Generator uses its own task editor with clear color and target-level controls.");
        slider(editor, "Output Level target peak in dBFS").setValue(-12, juce::sendNotificationSync);
        require(savedNumber(workspace, noiseId, "audio.param.levelDbRms") == -12,
                "Output Level keeps the existing project key so legacy noise settings remain compatible.");
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
