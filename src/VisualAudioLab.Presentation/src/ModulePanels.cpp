#include <val/presentation/ModulePanels.h>
#include <val/audio/AudioGraph.h>
#include <val/audio/ScriptNode.h>
#include <val/presentation/ShellTheme.h>
#include <val/script/Compiler.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>

namespace val::presentation
{
namespace
{
constexpr std::array<const char*, 14> catalogTypes{
    "Input", "Output", "Gain", "EQ", "Delay", "Compressor", "Distortion", "Chorus",
    "Reverb", "Oscilloscope", "Spectrum Analyzer", "Peak Meter", "RMS Meter", "Script"};

constexpr const char* passThrough = R"(NODE PassThrough AT AUDIO;
INPUT InSample: REAL;
OUTPUT OutSample: REAL;
BEGIN
  OutSample := InSample
END.
)";
constexpr const char* gainExample = R"(NODE ScriptGain AT AUDIO;
INPUT InSample: REAL;
OUTPUT OutSample: REAL;
PARAM Gain: REAL = 0.5 RANGE 0.0 .. 2.0;
BEGIN
  OutSample := InSample * Gain
END.
)";
constexpr const char* constantExample = R"(// Constant DC source, not an oscillator.
NODE ConstantSource AT AUDIO;
OUTPUT OutSample: REAL;
PARAM Level: REAL = 0.125 RANGE -1.0 .. 1.0;
BEGIN
  OutSample := Level
END.
)";
constexpr const char* stereoExample = R"(NODE StereoGate AT AUDIO;
INPUT LeftIn: REAL;
INPUT RightIn: REAL;
OUTPUT LeftOut: REAL;
OUTPUT RightOut: REAL;
PARAM Threshold: REAL = 0.1 RANGE 0.0 .. 1.0;
FUNCTION Gate(Value: REAL): REAL;
BEGIN
  IF (Value >= Threshold) OR (Value <= -Threshold) THEN
    Gate := Value
  ELSE
    Gate := 0.0
END;
BEGIN
  LeftOut := Gate(LeftIn);
  RightOut := Gate(RightIn)
END.
)";

std::unique_ptr<audio::AudioNode> prototype(const std::string& type)
{
    if (type == "Input") return std::make_unique<audio::InputNode>();
    if (type == "Output") return std::make_unique<audio::OutputNode>();
    if (type == "Gain") return std::make_unique<audio::GainNode>();
    if (type == "EQ") return std::make_unique<audio::EqNode>();
    if (type == "Delay") return std::make_unique<audio::DelayNode>();
    if (type == "Compressor") return std::make_unique<audio::CompressorNode>();
    if (type == "Distortion") return std::make_unique<audio::DistortionNode>();
    if (type == "Chorus") return std::make_unique<audio::ChorusNode>();
    if (type == "Reverb") return std::make_unique<audio::ReverbNode>();
    if (type == "Analyzer") return std::make_unique<audio::AnalyzerNode>();
    return {};
}

juce::String diagnostics(const script::CompilationResult<std::shared_ptr<const script::Program>>& result)
{
    juce::String text;
    for (const auto& error : result.diagnostics)
        text += "Line " + juce::String(error.source.line) + ", column "
            + juce::String(error.source.column) + ": " + juce::String(error.message) + "\n";
    return text.isEmpty() ? "Compilation failed without a verified program." : text.trimEnd();
}

bool parseNumber(const juce::String& text, double& value)
{
    const auto bytes = text.trim().toStdString();
    if (bytes.empty()) return false;
    const auto result = std::from_chars(bytes.data(), bytes.data() + bytes.size(), value);
    return result.ec == std::errc{} && result.ptr == bytes.data() + bytes.size() && std::isfinite(value);
}
}

/** Search filtering retains real list selection; activation is explicit via button, Enter or double-click. */
class ModuleCatalog::Impl final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Impl(ModuleCatalog& owner) : owner_(owner)
    {
        search_.setTitle("Search modules");
        search_.setTextToShowWhenEmpty("Search modules...", juce::Colours::grey);
        search_.onTextChange = [this] { filter(); };
        search_.onReturnKey = [this] { insert(); };
        list_.setModel(this);
        list_.setName("Available modules");
        list_.setRowHeight(30);
        insert_.onClick = [this] { insert(); };
        for (auto* child : std::initializer_list<juce::Component*>{&search_, &list_, &insert_, &status_})
            addAndMakeVisible(child);
        filter();
    }
    ~Impl() override { list_.setModel(nullptr); }
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        search_.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(6);
        insert_.setBounds(bounds.removeFromBottom(30));
        status_.setBounds(bounds.removeFromBottom(26));
        list_.setBounds(bounds);
    }
private:
    int getNumRows() override { return static_cast<int>(filtered_.size()); }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) graphics.fillAll(ShellTheme::accent().withAlpha(0.25f));
        graphics.setColour(juce::Colours::white);
        graphics.drawText(filtered_[static_cast<std::size_t>(row)], 8, 0, width - 16, height,
                          juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override { insert_.setEnabled(row >= 0 && row < getNumRows()); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { insert(); }
    void returnKeyPressed(int) override { insert(); }
    void filter()
    {
        const auto previous = list_.getSelectedRow();
        const auto selected = previous >= 0 && previous < getNumRows()
            ? filtered_[static_cast<std::size_t>(previous)] : juce::String{};
        filtered_.clear();
        for (const auto* type : catalogTypes)
            if (juce::String(type).containsIgnoreCase(search_.getText().trim())) filtered_.emplace_back(type);
        list_.updateContent();
        list_.deselectAllRows();
        const auto found = std::find(filtered_.begin(), filtered_.end(), selected);
        if (!filtered_.empty())
            list_.selectRow(found == filtered_.end() ? 0 : static_cast<int>(found - filtered_.begin()));
        insert_.setEnabled(!filtered_.empty());
        status_.setText(juce::String(getNumRows()) + " modules", juce::dontSendNotification);
    }
    void insert()
    {
        const auto row = list_.getSelectedRow();
        if (row < 0 || row >= getNumRows()) return;
        const auto type = filtered_[static_cast<std::size_t>(row)].toStdString();
        auto callback = owner_.onInsert;
        if (callback) callback(type);
        else status_.setText("Insertion is not connected.", juce::dontSendNotification);
    }
    ModuleCatalog& owner_;
    juce::TextEditor search_;
    juce::ListBox list_;
    juce::TextButton insert_{"Insert selected module"};
    juce::Label status_;
    std::vector<juce::String> filtered_;
};

ModuleCatalog::ModuleCatalog() : impl_(std::make_unique<Impl>(*this))
{
    setName("Module catalog");
    addAndMakeVisible(*impl_);
}
ModuleCatalog::~ModuleCatalog() = default;
void ModuleCatalog::resized() { impl_->setBounds(getLocalBounds()); }

/** Scrollable, selected-node editor with document edits separated from transient parameter layers. */
class ModuleEditor::Impl final : public juce::Component
{
public:
    Impl(ModuleEditor& owner, application::WorkspaceService& workspace) : owner_(owner), workspace_(workspace)
    {
        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(status_);
        status_.setMultiLine(true);
        status_.setReadOnly(true);
        status_.setTitle("Module editor diagnostics");
        title_.setJustificationType(juce::Justification::centredLeft);
        bypass_.onClick = [this] { saveBypass(); };
        explanation_.setText("Saved base is undoable. Override wins over automation; release restores automation/base. "
                             "Disable automation restores base after override release. Live targets are not saved.",
                             juce::dontSendNotification);
        explanation_.setMinimumHorizontalScale(1.0f);
        scheduleLabel_.setText("Schedule: seconds ahead", juce::dontSendNotification);
        seconds_.setText("1.0", false);
        seconds_.setTitle("Seconds ahead for scheduled automation target");
        seconds_.setTooltip("Nonnegative seconds ahead of the host's current running session position. "
                            "The host validates epoch, revision, queue capacity and ordering.");
        source_.setTitle("VAL Script source draft");
        sourceLabel_.setText("Source draft - not saved until Compile / Apply", juce::dontSendNotification);
        source_.setMultiLine(true, true);
        source_.setReturnKeyStartsNewLine(true);
        source_.setTabKeyUsedAsCharacter(true);
        source_.setScrollbarsShown(true);
        source_.setFont(juce::Font(juce::FontOptions(13.0f)));
        source_.onTextChange = [this]
        {
            if (!loadingSource_)
            {
                rememberDraft();
                status("Source draft changed; Compile / Apply is required. Saved-source parameters remain active.");
            }
        };
        examples_.setTextWhenNothingSelected("Load example into draft...");
        examples_.addItem("Mono pass-through (safe default)", 1);
        examples_.addItem("Mono gain", 2);
        examples_.addItem("Mono constant DC source", 3);
        examples_.addItem("Stereo gate", 4);
        examples_.onChange = [this]
        {
            const auto id = examples_.getSelectedId();
            if (id == 0) return;
            source_.setText(id == 1 ? passThrough : id == 2 ? gainExample : id == 3 ? constantExample : stereoExample);
            examples_.setSelectedId(0, juce::dontSendNotification);
        };
        compile_.onClick = [this] { applyScript(); };
        load_.onClick = [this] { loadSource(); };
        revert_.onClick = [this]
        {
            if (!editable() || !script_) return;
            drafts_.erase({patch_, selected_});
            script_ = false;
            refresh();
            status(savedSource_ ? "Draft discarded. The currently saved source is shown."
                                : "Draft discarded. No source is saved; the safe default draft is shown.");
        };
        for (auto* child : std::initializer_list<juce::Component*>{
                 &title_, &bypass_, &explanation_, &scheduleLabel_, &seconds_, &sourceLabel_, &source_,
                 &examples_, &compile_, &load_, &revert_})
            content_.addAndMakeVisible(child);
        refresh();
    }

    void status(const juce::String& message) { status_.setText(message, false); }
    void setNode(std::uint64_t id)
    {
        rememberDraft();
        script_ = false;
        selected_ = id;
        refresh();
    }
    std::uint64_t node() const noexcept { return selected_; }
    void refresh()
    {
        rememberDraft();
        ++generation_;
        rows_.clear();
        processor_.reset();
        program_.reset();
        script_ = false;
        valid_ = false;
        patch_ = workspace_.project().activePatch;
        const auto* selected = current();
        title_.setText(selected ? juce::String(selected->name) : "Module editor", juce::dontSendNotification);
        bypass_.setEnabled(false);
        if (!selected)
            status(selected_ == 0 ? "Select an audio module to edit." : "Selected node no longer exists.");
        else
        {
            const auto field = selected->parameters.find("audio.type");
            const auto* type = field != selected->parameters.end() ? std::get_if<std::string>(&field->second) : nullptr;
            if (!type) status("Cannot edit a generic node: an explicit audio.type string is required.");
            else
            {
                type_ = *type;
                script_ = type_ == "Script";
                if (script_)
                {
                    const auto sourceField = selected->parameters.find("audio.script");
                    const auto* saved = sourceField == selected->parameters.end()
                        ? nullptr : std::get_if<std::string>(&sourceField->second);
                    savedSource_ = saved ? std::optional<std::string>(*saved) : std::nullopt;
                    const auto key = std::pair{patch_, selected_};
                    auto draft = drafts_.find(key);
                    if (draft != drafts_.end() && saved && draft->second.text == *saved)
                    {
                        drafts_.erase(draft);
                        draft = drafts_.end();
                    }
                    loadingSource_ = true;
                    source_.setText(draft != drafts_.end() ? juce::String(draft->second.text)
                                                         : saved ? juce::String(*saved) : juce::String(passThrough), false);
                    loadingSource_ = false;
                    if (!saved) status("No saved script source. A safe mono pass-through draft is ready; Compile / Apply to save.");
                    else
                    {
                        const auto result = script::Compiler::compile(*saved);
                        if (!result.value) status(diagnostics(result));
                        else
                        {
                            program_ = *result.value;
                            processor_ = std::make_unique<audio::ScriptNode>(program_);
                        }
                    }
                }
                else processor_ = prototype(type_);

                if (processor_)
                {
                    domain::Graph isolated;
                    isolated.nodes.emplace(selected_, *selected);
                    const auto validation = audio::AudioGraph::fromStructural(isolated, 1);
                    if (!validation.value) status(juce::String(validation.error.message));
                    else
                    {
                        valid_ = true;
                        bypass_.setEnabled(true);
                        const auto bypass = selected->parameters.find("audio.bypass");
                        bypass_.setToggleState(bypass != selected->parameters.end() && std::get<bool>(bypass->second),
                                               juce::dontSendNotification);
                        addRows(*selected);
                        status(script_ ? "Saved source verified. Script INPUT/OUTPUT counts must match the runtime bus; "
                                         "mono examples require a mono bus."
                                       : "Saved base values are shown; live automation/override state is owned by the runtime.");
                    }
                }
                else if (!script_) status("Unsupported explicit audio.type: " + juce::String(type_));
                if (script_)
                    if (const auto draft = drafts_.find({patch_, selected_}); draft != drafts_.end())
                        status(draft->second.base == savedSource_
                            ? "Unsaved source draft preserved. Compile / Apply to save; parameter controls describe saved source."
                            : "Saved source changed while this draft was open. Unsaved draft preserved; Compile / Apply "
                              "explicitly replaces the saved source. Parameter controls describe saved source.");
            }
        }
        source_.setVisible(script_);
        sourceLabel_.setVisible(script_);
        examples_.setVisible(script_);
        compile_.setVisible(script_);
        load_.setVisible(script_);
        revert_.setVisible(script_);
        bypass_.setVisible(valid_);
        explanation_.setVisible(valid_);
        scheduleLabel_.setVisible(valid_ && !rows_.empty());
        seconds_.setVisible(valid_ && !rows_.empty());
        resized();
    }
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        status_.setBounds(bounds.removeFromBottom(90));
        bounds.removeFromBottom(5);
        viewport_.setBounds(bounds);
        const int width = std::max(240, viewport_.getWidth() - viewport_.getScrollBarThickness() - 6);
        int y = 0;
        title_.setBounds(4, y, width - 8, 28); y += 32;
        if (valid_)
        {
            bypass_.setBounds(4, y, width - 8, 28); y += 32;
            explanation_.setBounds(4, y, width - 8, 84); y += 88;
        }
        if (script_)
        {
            examples_.setBounds(4, y, width - 8, 28); y += 32;
            sourceLabel_.setBounds(4, y, width - 8, 26); y += 30;
            source_.setBounds(4, y, width - 8, 230); y += 234;
            compile_.setBounds(4, y, (width - 12) / 2, 28);
            load_.setBounds(8 + (width - 12) / 2, y, (width - 12) / 2, 28); y += 36;
            revert_.setBounds(4, y, width - 8, 28); y += 36;
        }
        if (valid_ && !rows_.empty())
        {
            scheduleLabel_.setBounds(4, y, width - 8, 24); y += 24;
            seconds_.setBounds(4, y, width - 8, 26); y += 32;
        }
        for (const auto& row : rows_)
        {
            row->setBounds(4, y, width - 8, 166);
            y += 172;
        }
        content_.setSize(width, std::max(y, bounds.getHeight()));
    }

private:
    /** Metadata-derived saved-base slider plus explicit transient override and automation targets. */
    class ParameterRow final : public juce::Component
    {
    public:
        ParameterRow(Impl& owner, std::uint16_t index, const audio::ParameterSpec& spec,
                     std::string key, double saved)
            : owner_(owner), index_(index), minimum_(spec.minimum), maximum_(spec.maximum), key_(std::move(key))
        {
            label_.setText(juce::String(spec.name) + "  [" + juce::String(spec.minimum) + " .. "
                               + juce::String(spec.maximum) + "]  saved base",
                           juce::dontSendNotification);
            label_.setTooltip("Physical units and smoothing are defined by the processor; ramp duration "
                              + juce::String(spec.smoothingSeconds * 1000.0) + " ms.");
            base_.setRange(spec.minimum, spec.maximum);
            base_.setSliderStyle(juce::Slider::LinearHorizontal);
            base_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 24);
            base_.setNumDecimalPlacesToDisplay(6);
            base_.setValue(saved, juce::dontSendNotification);
            base_.setTitle(juce::String(spec.name) + " saved base value");
            base_.onDragStart = [this] { dragging_ = true; };
            base_.onValueChange = [this]
            {
                if (!dragging_) owner_.deferSaveBase(index_, key_, base_.getValue());
            };
            base_.onDragEnd = [this]
            {
                dragging_ = false;
                owner_.deferSaveBase(index_, key_, base_.getValue());
            };
            targetLabel_.setText("Live target (not saved)", juce::dontSendNotification);
            target_.setText(juce::String(saved, 6), false);
            target_.setTitle(juce::String(spec.name) + " live override or automation target");
            override_.onClick = [this] { apply(audio::ParameterAction::overrideValue, false); };
            release_.onClick = [this] { owner_.runtime(index_, 0, audio::ParameterAction::releaseOverride); };
            automation_.onClick = [this] { apply(audio::ParameterAction::automation, false); };
            disable_.onClick = [this] { owner_.runtime(index_, 0, audio::ParameterAction::disableAutomation); };
            schedule_.onClick = [this] { apply(audio::ParameterAction::automation, true); };
            for (auto* child : std::initializer_list<juce::Component*>{
                     &label_, &base_, &targetLabel_, &target_, &override_, &release_, &automation_, &disable_, &schedule_})
                addAndMakeVisible(child);
        }
        void resized() override
        {
            auto bounds = getLocalBounds();
            label_.setBounds(bounds.removeFromTop(24));
            base_.setBounds(bounds.removeFromTop(30));
            auto targetLine = bounds.removeFromTop(26);
            target_.setBounds(targetLine.removeFromRight(std::min(110, targetLine.getWidth() / 2)));
            targetLabel_.setBounds(targetLine);
            bounds.removeFromTop(4);
            auto line = bounds.removeFromTop(26);
            override_.setBounds(line.removeFromLeft(line.getWidth() / 2).reduced(1, 0));
            release_.setBounds(line.reduced(1, 0));
            bounds.removeFromTop(3);
            line = bounds.removeFromTop(26);
            automation_.setBounds(line.removeFromLeft(line.getWidth() / 2).reduced(1, 0));
            disable_.setBounds(line.reduced(1, 0));
            bounds.removeFromTop(3);
            schedule_.setBounds(bounds.removeFromTop(24));
        }
    private:
        void apply(audio::ParameterAction action, bool scheduled)
        {
            double value{};
            if (!parseNumber(target_.getText(), value) || value < minimum_ || value > maximum_)
            {
                owner_.status("Live target must be a finite number within the displayed parameter range.");
                return;
            }
            if (scheduled) owner_.schedule(index_, static_cast<float>(value));
            else owner_.runtime(index_, static_cast<float>(value), action);
        }
        Impl& owner_;
        std::uint16_t index_{};
        double minimum_{}, maximum_{};
        std::string key_;
        bool dragging_{};
        juce::Label label_, targetLabel_;
        juce::Slider base_;
        juce::TextEditor target_;
        juce::TextButton override_{"Apply override"}, release_{"Release override"},
            automation_{"Automation now"}, disable_{"Disable automation"}, schedule_{"Schedule automation target"};
    };

    /** A node-local draft retains the saved baseline so external source changes are visible. */
    struct Draft
    {
        std::optional<std::string> base;
        std::string text;
    };
    void rememberDraft()
    {
        if (!script_ || selected_ == 0) return;
        const auto text = source_.getText().toStdString();
        const auto key = std::pair{patch_, selected_};
        if (text == savedSource_.value_or(passThrough))
        {
            drafts_.erase(key);
            return;
        }
        const auto found = drafts_.find(key);
        if (found == drafts_.end()) drafts_.emplace(key, Draft{savedSource_, text});
        else found->second.text = text;
    }
    void deferSaveBase(std::uint16_t parameter, std::string key, double value)
    {
        const juce::Component::SafePointer<ModuleEditor> safe(&owner_);
        const auto generation = generation_;
        // A host refresh can destroy sliders. Wait until JUCE finishes its drag/text callback stack.
        juce::MessageManager::callAsync([safe, generation, parameter, key = std::move(key), value]
        {
            if (safe && safe->impl_->generation_ == generation)
                safe->impl_->saveBase(parameter, key, value);
        });
    }
    const domain::Node* current() const
    {
        if (workspace_.project().activePatch != patch_) return nullptr;
        const auto found = workspace_.graph().nodes.find(selected_);
        return found == workspace_.graph().nodes.end() ? nullptr : &found->second;
    }
    bool editable()
    {
        const auto* selected = current();
        if (selected)
        {
            const auto found = selected->parameters.find("audio.type");
            if (found != selected->parameters.end())
                if (const auto* type = std::get_if<std::string>(&found->second); type && *type == type_) return true;
        }
        status("Selection, patch or module type changed. Refresh the editor before editing.");
        return false;
    }
    void addRows(const domain::Node& node)
    {
        const auto parameters = processor_->parameters();
        for (std::size_t i = 0; i < parameters.size(); ++i)
        {
            const auto& spec = parameters[i];
            std::string key = std::string("audio.param.") + spec.name;
            if (type_ == "Gain" && node.parameters.contains("audio.gain")) key = "audio.gain";
            if (script_)
                for (const auto& [candidate, ignored] : node.parameters)
                {
                    (void) ignored;
                    if (candidate.starts_with("audio.param.")
                        && juce::String(candidate.substr(12)).equalsIgnoreCase(spec.name))
                    {
                        key = candidate;
                        break;
                    }
                }
            const auto found = node.parameters.find(key);
            const double value = found == node.parameters.end() ? spec.initial : std::get<double>(found->second);
            auto row = std::make_unique<ParameterRow>(*this, static_cast<std::uint16_t>(i), spec, key, value);
            content_.addAndMakeVisible(*row);
            rows_.push_back(std::move(row));
        }
    }
    void saveBase(std::uint16_t parameter, const std::string& key, double value)
    {
        if (!editable() || !processor_) return;
        if (parameter >= processor_->parameters().size()) return;
        const auto& spec = processor_->parameters()[parameter];
        if (!std::isfinite(value) || value < spec.minimum || value > spec.maximum)
        {
            status("Saved base must be finite and within the displayed parameter range.");
            return;
        }
        const auto id = selected_;
        auto changedCallback = owner_.onChanged;
        const auto result = workspace_.setParameter(id, key, value);
        if (!result.succeeded) { status(juce::String(result.error)); return; }
        status(changedCallback ? "Saved base updated. Overrides/automation may still take precedence."
                               : "Saved base updated (offline; host synchronization is not connected).");
        if (changedCallback) changedCallback();
    }
    void saveBypass()
    {
        if (!editable()) return;
        const auto value = bypass_.getToggleState();
        const auto id = selected_;
        auto changedCallback = owner_.onChanged;
        const auto result = workspace_.setParameter(id, "audio.bypass", value);
        if (!result.succeeded) { status(juce::String(result.error)); return; }
        status(changedCallback ? "Saved bypass updated." : "Saved bypass updated (offline; host synchronization is not connected).");
        if (changedCallback) changedCallback();
    }
    void runtime(std::uint16_t parameter, float value, audio::ParameterAction action)
    {
        if (!editable()) return;
        auto callback = owner_.onRuntimeParameter;
        if (!callback) { status("Runtime action unavailable: the host has not connected the runtime callback."); return; }
        status("Runtime transition requested; saved base is unchanged.");
        callback(selected_, parameter, value, action);
    }
    void schedule(std::uint16_t parameter, float value)
    {
        if (!editable()) return;
        auto callback = owner_.onScheduleParameter;
        if (!callback) { status("Scheduling unavailable: the host has not connected the scheduler."); return; }
        double seconds{};
        if (!parseNumber(seconds_.getText(), seconds) || seconds < 0)
        {
            status("Schedule delay must be a finite nonnegative number of seconds.");
            return;
        }
        status("Scheduled automation requested from host; saved base is unchanged.");
        callback(selected_, parameter, value, seconds);
    }
    void applyScript()
    {
        if (!editable() || !script_) return;
        const auto source = source_.getText().toStdString();
        const auto result = script::Compiler::compile(source);
        if (!result.value) { status(diagnostics(result)); return; }
        auto replacement = *current();
        replacement.parameters.insert_or_assign("audio.script", source);
        domain::Graph isolated;
        isolated.nodes.emplace(selected_, std::move(replacement));
        const auto validation = audio::AudioGraph::fromStructural(isolated, 1);
        if (!validation.value)
        {
            status("Compiled, but not applied: " + juce::String(validation.error.message)
                + "\nSaved parameter names/ranges must remain compatible. No saved state was changed.");
            return;
        }
        auto callback = owner_.onChanged;
        const auto saved = workspace_.setParameter(selected_, "audio.script", source);
        if (!saved.succeeded) { status(juce::String(saved.error)); return; }
        drafts_.erase({patch_, selected_});
        savedSource_ = source;
        refresh();
        status("Compiled and applied. Saved source and its verified parameters are shown. "
               "INPUT/OUTPUT counts must match the runtime bus.");
        if (callback) callback();
    }
    void loadSource()
    {
        if (!editable() || !script_) return;
        chooser_ = std::make_unique<juce::FileChooser>("Load VAL Script source into draft",
                                                      juce::File{}, "*.vals;*.val;*.txt");
        const juce::Component::SafePointer<ModuleEditor> safe(&owner_);
        const auto generation = generation_;
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, generation](const juce::FileChooser& chooser)
            {
                if (!safe || safe->impl_->generation_ != generation) return;
                auto& editor = *safe->impl_;
                if (!editor.editable()) return;
                const auto file = chooser.getResult();
                if (file == juce::File{}) return;
                auto stream = file.createInputStream();
                if (!stream || stream->failedToOpen())
                {
                    editor.status("Cannot open source file for reading.");
                    return;
                }
                if (stream->getTotalLength() > 65536)
                {
                    editor.status("Source exceeds the compiler's 64 KiB limit.");
                    return;
                }
                const auto source = stream->readEntireStreamAsString();
                if (stream->getStatus().failed())
                {
                    editor.status("Source read failed: " + stream->getStatus().getErrorMessage());
                    return;
                }
                editor.source_.setText(source);
                editor.status("Source loaded into draft only; Compile / Apply validates before saving.");
            });
    }

    ModuleEditor& owner_;
    application::WorkspaceService& workspace_;
    std::uint64_t selected_{}, patch_{}, generation_{};
    std::string type_;
    std::optional<std::string> savedSource_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Draft> drafts_;
    bool valid_{}, script_{}, loadingSource_{};
    juce::Component content_;
    juce::Viewport viewport_;
    juce::TextEditor status_, source_, seconds_;
    juce::Label title_, explanation_, scheduleLabel_, sourceLabel_;
    juce::ToggleButton bypass_{"Saved bypass (dry for effects; silence for sources)"};
    juce::ComboBox examples_;
    juce::TextButton compile_{"Compile / Apply"}, load_{"Load Source..."}, revert_{"Revert to saved source"};
    std::shared_ptr<const script::Program> program_;
    std::unique_ptr<audio::AudioNode> processor_;
    std::vector<std::unique_ptr<ParameterRow>> rows_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

ModuleEditor::ModuleEditor(application::WorkspaceService& workspace)
    : impl_(std::make_unique<Impl>(*this, workspace))
{
    setName("Module editor");
    addAndMakeVisible(*impl_);
}
ModuleEditor::~ModuleEditor() = default;
void ModuleEditor::resized() { impl_->setBounds(getLocalBounds()); }
void ModuleEditor::setNode(std::uint64_t id) { impl_->setNode(id); }
std::uint64_t ModuleEditor::node() const noexcept { return impl_->node(); }
void ModuleEditor::refresh() { impl_->refresh(); }
void ModuleEditor::setStatusMessage(const juce::String& message) { impl_->status(message); }
std::string ModuleEditor::defaultScriptSource() { return passThrough; }
}
