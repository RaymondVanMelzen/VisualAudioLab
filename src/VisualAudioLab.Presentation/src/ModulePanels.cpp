#include <val/presentation/ModulePanels.h>
#include <val/presentation/ModuleCatalogItems.h>
#include <val/audio/AudioGraph.h>
#include <val/audio/GeneratorNodes.h>
#include <val/audio/ScriptNode.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>
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
    if (type == "Sum") return std::make_unique<audio::SumNode>();
    if (type == "Gain") return std::make_unique<audio::GainNode>();
    if (type == "EQ") return std::make_unique<audio::EqNode>();
    if (type == "Delay") return std::make_unique<audio::DelayNode>();
    if (type == "Compressor") return std::make_unique<audio::CompressorNode>();
    if (type == "Distortion") return std::make_unique<audio::DistortionNode>();
    if (type == "Chorus") return std::make_unique<audio::ChorusNode>();
    if (type == "Reverb") return std::make_unique<audio::ReverbNode>();
    if (type == "Analyzer") return std::make_unique<audio::AnalyzerNode>();
    if (type == "Signal Generator") return std::make_unique<audio::SignalGeneratorNode>();
    if (type == "Impulse Generator") return std::make_unique<audio::ImpulseGeneratorNode>();
    if (type == "Click Generator") return std::make_unique<audio::ClickGeneratorNode>();
    if (type == "Burst Generator") return std::make_unique<audio::BurstGeneratorNode>();
    if (type == "Chirp Generator") return std::make_unique<audio::ChirpGeneratorNode>();
    if (type == "Noise Generator") return std::make_unique<audio::NoiseGeneratorNode>();
    if (type == "DC Generator") return std::make_unique<audio::DcGeneratorNode>();
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

std::vector<std::pair<juce::String, float>> parameterChoices(const std::string& type, std::string_view name)
{
    if (name == "waveform")
        return {{"Sine", 0.0f}, {"Square (aliased)", 1.0f}, {"Triangle (aliased)", 2.0f},
                {"Sawtooth (aliased)", 3.0f}, {"White Noise", 4.0f}, {"Pink Noise", 5.0f}, {"DC", 6.0f}};
    if (name == "mode" && type == "Signal Generator")
        return {{"Fixed Frequency", 0.0f}, {"Continuous Sweep", 1.0f}, {"Musical Sweep", 2.0f}};
    if (name == "mode" && (type == "Impulse Generator" || type == "Burst Generator"))
        return {{"One-shot", 0.0f}, {"Periodic", 1.0f}};
    if (name == "sweepCurve")
        return {{"Linear (Hz)", 0.0f}, {"Logarithmic (exponential Hz/time)", 1.0f}, {"Exponential curvature", 2.0f}};
    if (name == "scale")
        return {{"Chromatic", 0.0f}, {"Major", 1.0f}, {"Natural Minor", 2.0f},
                {"Major Pentatonic", 3.0f}, {"Minor Pentatonic", 4.0f}, {"User Scale", 5.0f}};
    if (name == "transport")
        return {{"Free Run", 0.0f}, {"One-shot", 1.0f}, {"Loop", 2.0f}};
    if (name == "color")
        return {{"White Noise", 0.0f}, {"Pink Noise", 1.0f}};
    if (name == "intervalMode")
        return {{"BPM", 0.0f}, {"Explicit Interval", 1.0f}};
    if (name == "muted")
        return {{"Unmuted", 0.0f}, {"Muted", 1.0f}};
    if (name == "phaseInvert")
        return {{"Normal", 0.0f}, {"Inverted", 1.0f}};
    return {};
}

juce::String parameterUnit(std::string_view name)
{
    if (name.find("Frequency") != std::string_view::npos || name == "frequencyHz") return "Hz";
    if (name.find("Seconds") != std::string_view::npos || name == "stepSeconds"
        || name == "intervalSeconds" || name == "fadeSeconds") return "s";
    if (name == "levelDb") return "dBFS peak";
    if (name == "levelDbRms") return "dBFS peak reference";
    if (name == "bpm") return "BPM";
    if (name == "rootMidi") return "MIDI note";
    return {};
}

juce::String displayParameterName(const std::string& type, std::string_view name)
{
    if (name == "frequencyHz") return "Frequency";
    if (name == "startFrequencyHz") return "Start Frequency";
    if (name == "endFrequencyHz") return "End Frequency";
    if (name == "durationSeconds") return type == "Signal Generator" || type == "Chirp Generator"
        ? "Sweep Time" : "Duration";
    if (name == "stepSeconds") return "Step Time";
    if (name == "levelDb" || name == "levelDbRms") return "Output Level";
    if (name == "rootMidi") return "Root MIDI Note";
    if (name == "referenceAHz") return "Reference A";
    if (name == "color") return "Noise Color";
    if (name == "sweepCurve") return "Sweep Shape";
    if (name == "muted") return "Output";
    return juce::String(name.data(), static_cast<int>(name.size()));
}
}

/** Search filtering retains real list selection; activation is explicit via button, Enter or double-click. */
class ModuleCatalog::Impl final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Impl(ModuleCatalog& owner) : owner_(owner)
    {
        search_.setTitle("Search modules");
        search_.setFont(Typography::ui(12));
        search_.setTextToShowWhenEmpty("Search modules...", ShellTheme::textMuted());
        search_.onTextChange = [this] { filter(); };
        search_.onReturnKey = [this] { insert(); };
        category_.setTitle("Module category");
        for (std::size_t index = 0; index < moduleCategoryNames.size(); ++index)
            category_.addItem(juce::String(moduleCategoryNames[index].data(),
                                          static_cast<int>(moduleCategoryNames[index].size())),
                              static_cast<int>(index) + 1);
        category_.setSelectedId(1, juce::dontSendNotification);
        category_.onChange = [this] { filter(); };
        list_.setModel(this);
        list_.setName("Available modules");
        list_.setRowHeight(30);
        list_.addMouseListener(this, true);
        insert_.onClick = [this] { insert(); };
        help_.setText("Browser: processors, generators, Sum.\nHardware I/O: fixed canvas lanes.", juce::dontSendNotification);
        help_.setFont(Typography::ui(12));
        for (auto* child : std::initializer_list<juce::Component*>{&search_, &category_, &list_, &insert_, &status_, &help_})
            addAndMakeVisible(child);
        filter();
    }
    ~Impl() override { list_.removeMouseListener(this); list_.setModel(nullptr); }
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        help_.setBounds(bounds.removeFromTop(42));
        category_.setBounds(bounds.removeFromTop(28));
        search_.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(6);
        insert_.setBounds(bounds.removeFromBottom(30));
        status_.setBounds(bounds.removeFromBottom(26));
        list_.setBounds(bounds);
    }
private:
    int getNumRows() override { return static_cast<int>(filtered_.size()); }
    juce::String getTooltipForRow(int row) override
    {
        return row >= 0 && row < getNumRows()
            ? juce::String(filtered_[static_cast<std::size_t>(row)]->description.data(),
                           static_cast<int>(filtered_[static_cast<std::size_t>(row)]->description.size()))
            : juce::String{};
    }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected)
        {
            graphics.fillAll(ShellTheme::raised());
            graphics.setColour(ShellTheme::selected());
            graphics.fillRect(0, 0, 3, height);
        }
        graphics.setColour(ShellTheme::textPrimary());
        graphics.setFont(Typography::font(TextRole::menu));
        const auto& entry = *filtered_[static_cast<std::size_t>(row)];
        const auto categoryIndex = static_cast<std::size_t>(entry.category);
        const auto label = juce::String(entry.name.data(), static_cast<int>(entry.name.size()))
            + "  |  " + juce::String(moduleCategoryNames[categoryIndex].data(),
                                     static_cast<int>(moduleCategoryNames[categoryIndex].size()));
        graphics.drawText(label, 8, 0, width - 16, height,
                          juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override { insert_.setEnabled(row >= 0 && row < getNumRows()); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { insert(); }
    void returnKeyPressed(int) override { insert(); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto relative = event.getEventRelativeTo(&list_).position.toInt();
        dragRow_ = list_.getRowContainingPosition(relative.x, relative.y);
        dragStarted_ = false;
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragRow_ < 0 || dragRow_ >= getNumRows() || dragStarted_ || event.getDistanceFromDragStart() < 5)
            return;
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(&list_);
        if (!container)
        {
            status_.setText("Drag insertion is unavailable in this window.", juce::dontSendNotification);
            return;
        }
        const auto& entry = *filtered_[static_cast<std::size_t>(dragRow_)];
        const juce::String type(entry.name.data(), static_cast<int>(entry.name.size()));
        container->startDragging(type, &list_, juce::ScaledImage{}, false, nullptr, &event.source);
        dragStarted_ = true;
        status_.setText("Dragging " + type + " to the Patch Canvas.", juce::dontSendNotification);
    }
    void mouseUp(const juce::MouseEvent&) override { dragRow_ = -1; dragStarted_ = false; }
    void filter()
    {
        const auto previous = list_.getSelectedRow();
        const auto selected = previous >= 0 && previous < getNumRows()
            ? filtered_[static_cast<std::size_t>(previous)]->name : std::string_view{};
        filtered_.clear();
        const auto categoryId = category_.getSelectedId() - 1;
        const auto selectedCategory = categoryId >= 0 && categoryId < static_cast<int>(moduleCategoryNames.size())
            ? static_cast<ModuleCategory>(categoryId) : ModuleCategory::all;
        const auto query = search_.getText().trim();
        for (const auto& entry : moduleCatalog)
        {
            const auto name = juce::String(entry.name.data(), static_cast<int>(entry.name.size()));
            if ((selectedCategory == ModuleCategory::all || entry.category == selectedCategory)
                && name.containsIgnoreCase(query))
                filtered_.push_back(&entry);
        }
        list_.updateContent();
        list_.deselectAllRows();
        const auto found = std::find_if(filtered_.begin(), filtered_.end(),
            [selected](const auto* entry) { return entry->name == selected; });
        if (!filtered_.empty())
            list_.selectRow(found == filtered_.end() ? 0 : static_cast<int>(found - filtered_.begin()));
        insert_.setEnabled(!filtered_.empty());
        const auto category = category_.getText();
        status_.setText(category + juce::String::fromUTF8(" · ") + juce::String(getNumRows())
                            + (getNumRows() == 1 ? " result" : " results"),
                        juce::dontSendNotification);
    }
    void insert()
    {
        const auto row = list_.getSelectedRow();
        if (row < 0 || row >= getNumRows()) return;
        const auto type = std::string(filtered_[static_cast<std::size_t>(row)]->name);
        auto callback = owner_.onInsert;
        if (callback) callback(type);
        else status_.setText("Insertion is not connected.", juce::dontSendNotification);
    }
    ModuleCatalog& owner_;
    juce::TextEditor search_;
    juce::ComboBox category_;
    juce::ListBox list_;
    juce::TextButton insert_{"Insert selected module"};
    juce::Label status_, help_;
    std::vector<const ModuleCatalogEntry*> filtered_;
    int dragRow_{-1};
    bool dragStarted_{};
};

ModuleCatalog::ModuleCatalog() : impl_(std::make_unique<Impl>(*this))
{
    setName("Module catalog");
    addAndMakeVisible(*impl_);
}
ModuleCatalog::~ModuleCatalog() = default;
void ModuleCatalog::resized() { impl_->setBounds(getLocalBounds()); }

/** Scrollable, selected-node editor with document edits separated from transient parameter layers. */
class GeneratorPreview final : public juce::Component
{
public:
    void setSignal(int mode, int waveform, float start, float end, int curve)
    { mode_ = mode; waveform_ = waveform; start_ = start; end_ = end; curve_ = curve; repaint(); }
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(ShellTheme::raised());
        g.fillRoundedRectangle(area, 7.0f);
        area = area.reduced(10.0f, 9.0f);
        g.setColour(ShellTheme::textMuted());
        g.setFont(Typography::font(TextRole::meterScale));
        g.drawText(juce::String::fromUTF8(mode_ == 1 ? "TARGET FREQUENCY VS TIME · PREVIEW"
                                                     : mode_ == 2 ? "MUSICAL SWEEP PREVIEW · SHAPE ONLY"
                                                                  : "WAVEFORM PREVIEW · SHAPE ONLY"),
                   area.removeFromTop(17.0f), juce::Justification::centredLeft);
        const auto plot = area.reduced(2.0f, 4.0f);
        g.setColour(ShellTheme::border());
        g.drawHorizontalLine(static_cast<int>(plot.getCentreY()), plot.getX(), plot.getRight());
        g.setColour(ShellTheme::audio());
        juce::Path path;
        constexpr int points = 120;
        if (mode_ == 2)
        {
            const auto noteY = [&](int note)
            { return plot.getCentreY() - (0.72f - static_cast<float>(note) * 0.24f) * plot.getHeight() * 0.42f; };
            path.startNewSubPath(plot.getX(), noteY(0));
            for (int note = 1; note < 7; ++note)
            {
                const auto x = plot.getX() + plot.getWidth() * static_cast<float>(note) / 6.0f;
                path.lineTo(x, noteY(note - 1));
                path.lineTo(x, noteY(note));
            }
            path.lineTo(plot.getRight(), noteY(6));
        }
        else
        {
            for (int i = 0; i < points; ++i)
            {
                const auto x = static_cast<float>(i) / static_cast<float>(points - 1);
                float y{};
                if (mode_ == 1)
                {
                    double shaped = x;
                    if (curve_ == 1)
                    {
                        const auto low = std::log(std::max(20.0f, std::min(start_, end_)));
                        const auto high = std::log(std::max(20.0f, std::max(start_, end_)));
                        const auto frequency = std::exp(low + (high - low) * x);
                        shaped = (frequency - std::min(start_, end_))
                               / std::max(0.0001f, std::max(start_, end_) - std::min(start_, end_));
                    }
                    else if (curve_ == 2)
                        shaped = std::expm1(3.0 * x) / std::expm1(3.0);
                    y = 1.0f - static_cast<float>(shaped) * 2.0f;
                }
                else if (waveform_ >= 4)
                    y = std::sin(i * 12.9898f) * 0.65f;
                else
                {
                    const auto phase = x * 8.0f;
                    switch (waveform_)
                    {
                        case 1: y = std::sin(phase * juce::MathConstants<float>::twoPi) >= 0 ? 0.75f : -0.75f; break;
                        case 2: y = 2.0f / juce::MathConstants<float>::pi
                            * std::asin(std::sin(phase * juce::MathConstants<float>::twoPi)); break;
                        case 3: y = 2.0f * (phase - std::floor(phase + 0.5f)); break;
                        default: y = std::sin(phase * juce::MathConstants<float>::twoPi); break;
                    }
                }
                const auto point = juce::Point<float>(plot.getX() + plot.getWidth() * x,
                    plot.getCentreY() - juce::jlimit(-1.0f, 1.0f, y) * plot.getHeight() * 0.42f);
                if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
            }
        }
        g.strokePath(path, juce::PathStrokeType(1.7f));
    }
private:
    int mode_{}, waveform_{}, curve_{};
    float start_{20}, end_{20000};
};

class GeneratorMeter final : public juce::Component
{
public:
    void setLevels(float peak, bool peakValid, float rms, bool rmsValid)
    { peak_ = peak; peakValid_ = peakValid; rms_ = rms; rmsValid_ = rmsValid; repaint(); }
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(ShellTheme::raised());
        g.fillRoundedRectangle(bounds, 6.0f);
        bounds = bounds.reduced(8.0f, 5.0f);
        const auto barHeight = (bounds.getHeight() - 6.0f) * 0.5f;
        const auto draw = [&](float db, bool valid, float y, juce::Colour colour)
        {
            auto bar = juce::Rectangle<float>(bounds.getX(), y, bounds.getWidth(), barHeight);
            g.setColour(ShellTheme::input());
            g.fillRoundedRectangle(bar, 2.0f);
            if (valid)
            {
                const auto proportion = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
                bar.setWidth(bar.getWidth() * proportion);
                g.setColour(db >= -1.0f ? ShellTheme::error() : colour);
                g.fillRoundedRectangle(bar, 2.0f);
            }
        };
        draw(peak_, peakValid_, bounds.getY(), ShellTheme::audio());
        draw(rms_, rmsValid_, bounds.getY() + barHeight + 6.0f, ShellTheme::analyzer());
    }
private:
    float peak_{-160.0f}, rms_{-160.0f};
    bool peakValid_{}, rmsValid_{};
};

class ModuleEditor::Impl final : public juce::Component, private juce::Timer
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
        title_.setFont(Typography::font(TextRole::panelTitle));
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
        source_.setFont(Typography::font(TextRole::code));
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
        frequencyEntryLabel_.setText("Frequency entry: Hz, MIDI 69, A4, or A4 (A=442 Hz)",
                                     juce::dontSendNotification);
        frequencyEntry_.setTitle("Convert Hz, MIDI note or named note to saved fixed frequency");
        frequencyEntry_.setTextToShowWhenEmpty("e.g. 440 Hz, MIDI 69, A4", ShellTheme::textSecondary());
        frequencyApply_.onClick = [this] { saveFrequencyEntry(); };
        scaleIntervalsLabel_.setText("Custom scale: ascending semitone offsets from root",
                                     juce::dontSendNotification);
        scaleIntervals_.setTitle("Comma-separated custom musical scale semitone offsets");
        scaleIntervals_.setTextToShowWhenEmpty("0, 2, 4, 5, 7, 9, 11", ShellTheme::textSecondary());
        scaleIntervals_.onTextChange = [this]
        {
            if (!loadingScale_) rememberScaleDraft();
        };
        scaleApply_.onClick = [this] { saveUserScale(); };
        controlTarget_.setTitle("Continuous parameter receiving a scalar control mapping");
        controlTarget_.setTextWhenNothingSelected("Choose a continuous parameter...");
        controlSource_.setTitle("Typed scalar source for the selected parameter");
        controlSource_.addItem("Off", 1);
        controlSource_.addItem("LFO", 2);
        controlSource_.addItem("One-shot Envelope", 3);
        controlSource_.addItem("VAL Script (Time -> scalar)", 4);
        controlSource_.setSelectedId(1, juce::dontSendNotification);
        controlSource_.onChange = [this] { updateControlVisibility(); };
        controlTarget_.onChange = [this] { loadControlEditor(); };
        controlOffset_.setTitle("Control mapping offset in destination parameter units");
        controlDepth_.setTitle("Control mapping depth in destination parameter units");
        controlRate_.setTitle("LFO rate in hertz");
        controlAttack_.setTitle("Envelope attack duration in seconds");
        controlHold_.setTitle("Envelope hold duration in seconds");
        controlRelease_.setTitle("Envelope release duration in seconds");
        controlMinimum_.setTitle("Declared minimum VAL Script scalar output");
        controlMaximum_.setTitle("Declared maximum VAL Script scalar output");
        controlScript_.setTitle("Stateless VAL Script scalar control source");
        controlScript_.setFont(Typography::font(TextRole::code));
        controlScript_.setMultiLine(true, true);
        controlScript_.setReturnKeyStartsNewLine(true);
        controlScript_.setScrollbarsShown(true);
        controlScript_.setText("NODE ScalarControl AT AUDIO;\nINPUT Time: REAL;\nOUTPUT Value: REAL;\nBEGIN\n  Value := Time * 0.1;\nEND.", false);
        controlOffset_.setTextToShowWhenEmpty("Offset", ShellTheme::textSecondary());
        controlDepth_.setTextToShowWhenEmpty("Depth", ShellTheme::textSecondary());
        controlRate_.setTextToShowWhenEmpty("Rate Hz", ShellTheme::textSecondary());
        controlAttack_.setTextToShowWhenEmpty("Attack s", ShellTheme::textSecondary());
        controlHold_.setTextToShowWhenEmpty("Hold s", ShellTheme::textSecondary());
        controlRelease_.setTextToShowWhenEmpty("Release s", ShellTheme::textSecondary());
        controlMinimum_.setTextToShowWhenEmpty("Output min", ShellTheme::textSecondary());
        controlMaximum_.setTextToShowWhenEmpty("Output max", ShellTheme::textSecondary());
        for (auto* editor : {&frequencyEntry_, &scaleIntervals_, &controlOffset_, &controlDepth_,
                             &controlRate_, &controlAttack_, &controlHold_, &controlRelease_,
                             &controlMinimum_, &controlMaximum_, &seconds_})
            editor->setFont(Typography::font(TextRole::value));
        controlSave_.onClick = [this] { saveControlEditor(); };
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
                 &examples_, &compile_, &load_, &revert_, &frequencyEntryLabel_, &frequencyEntry_,
                 &frequencyApply_, &scaleIntervalsLabel_, &scaleIntervals_, &scaleApply_})
            content_.addAndMakeVisible(child);
        for (auto* child : std::initializer_list<juce::Component*>{
                 &controlLabel_, &controlTarget_, &controlSource_, &controlOffset_,
                 &controlDepth_, &controlRate_, &controlAttack_, &controlHold_,
                 &controlRelease_, &controlMinimum_, &controlMaximum_, &controlScript_, &controlSave_})
            content_.addAndMakeVisible(child);
        for (auto* child : std::initializer_list<juce::Component*>{
                 &generatorGoalLabel_, &generatorGoal_, &generatorWaveformLabel_, &generatorWaveform_,
                 &generatorFrequencyLabel_, &generatorFrequency_, &generatorStartLabel_, &generatorStart_,
                 &generatorEndLabel_, &generatorEnd_, &generatorDurationLabel_, &generatorDuration_,
                 &generatorLevelLabel_, &generatorLevel_, &generatorNoiseLabel_, &generatorNoise_,
                 &generatorScaleLabel_, &generatorScale_, &generatorRootLabel_, &generatorRoot_,
                 &generatorStepLabel_, &generatorStep_, &generatorCurveLabel_, &generatorCurve_,
                 &generatorCalibrationLabel_, &generatorCalibration_, &generatorPreview_, &generatorMeter_,
                 &generatorPeak_, &generatorRms_, &generatorMeterState_, &generatorMute_, &generatorSafety_,
                 &advancedToggle_, &automationToggle_, &developerToggle_,
                 &developerDetails_})
            content_.addAndMakeVisible(child);
        configureGeneratorEditor();
        startTimerHz(20);
        refresh();
    }

    void status(const juce::String& message)
    {
        status_.setFont(script_ ? Typography::font(TextRole::diagnostics)
                                : type_.find("Generator") != std::string::npos
                                    ? Typography::ui(15) : Typography::ui(12));
        status_.setText(message, false);
    }
    void timerCallback() override { refreshGeneratorMeters(); }
    void configureGeneratorEditor()
    {
        generatorGoal_.addItem("Fixed tone", 1);
        generatorGoal_.addItem("Frequency sweep", 2);
        generatorGoal_.addItem("Musical sweep", 3);
        generatorGoal_.addItem("Noise", 4);
        generatorGoal_.addItem("Calibration signal", 5);
        generatorWaveform_.addItemList({"Sine", "Square", "Triangle", "Sawtooth", "White Noise", "Pink Noise", "DC"}, 1);
        generatorNoise_.addItem("White", 1);
        generatorNoise_.addItem("Pink", 2);
        generatorScale_.addItemList({"Chromatic", "Major", "Natural minor", "Major pentatonic",
                                     "Minor pentatonic", "Custom"}, 1);
        generatorCurve_.addItemList({"Linear", "Logarithmic", "Exponential"}, 1);
        generatorCalibration_.addItem(juce::String::fromUTF8("Reference tone · 1 kHz sine"), 1);
        generatorCalibration_.addItem("Pink-noise reference", 2);
        generatorGoal_.setTitle("Signal goal");
        generatorWaveform_.setTitle("Waveform");
        generatorNoise_.setTitle("Noise color");
        generatorScale_.setTitle("Musical scale");
        generatorCurve_.setTitle("Sweep shape");
        generatorCalibration_.setTitle("Calibration signal type");
        generatorMute_.setButtonText("Mute output");
        generatorMute_.setTitle("Mute generator output without stopping transport");
        generatorMute_.setTooltip("Mutes this source while leaving its transport running.");
        generatorMute_.onClick = [this]
        {
            if (!processor_) return;
            const auto& parameters = processor_->parameters();
            const auto muted = std::find_if(parameters.begin(), parameters.end(),
                [](const auto& parameter) { return std::string_view(parameter.name) == "muted"; });
            if (muted == parameters.end())
            {
                status("Mute is unavailable for this generator.");
                return;
            }
            saveBase(static_cast<std::uint16_t>(std::distance(parameters.begin(), muted)),
                     "audio.param.muted", generatorMute_.getToggleState() ? 1.0 : 0.0);
        };
        generatorGoalLabel_.setText("Start with a goal", juce::dontSendNotification);
        generatorWaveformLabel_.setText("Waveform", juce::dontSendNotification);
        generatorFrequencyLabel_.setText("Frequency", juce::dontSendNotification);
        generatorStartLabel_.setText("Start Frequency", juce::dontSendNotification);
        generatorEndLabel_.setText("End Frequency", juce::dontSendNotification);
        generatorDurationLabel_.setText("Sweep Time", juce::dontSendNotification);
        generatorLevelLabel_.setText(juce::String::fromUTF8("Output Level · target peak"), juce::dontSendNotification);
        generatorNoiseLabel_.setText("Noise Color", juce::dontSendNotification);
        generatorScaleLabel_.setText("Scale", juce::dontSendNotification);
        generatorRootLabel_.setText("Root MIDI Note", juce::dontSendNotification);
        generatorStepLabel_.setText("Step Time", juce::dontSendNotification);
        generatorCurveLabel_.setText("Sweep Shape", juce::dontSendNotification);
        generatorCalibrationLabel_.setText("Choose a digital reference", juce::dontSendNotification);
        for (auto* label : {&generatorGoalLabel_, &generatorWaveformLabel_, &generatorFrequencyLabel_,
                            &generatorStartLabel_, &generatorEndLabel_, &generatorDurationLabel_,
                            &generatorLevelLabel_, &generatorNoiseLabel_, &generatorScaleLabel_,
                            &generatorRootLabel_, &generatorStepLabel_})
            label->setFont(Typography::ui(16, true));
        generatorCurveLabel_.setFont(Typography::ui(16, true));
        generatorCalibrationLabel_.setFont(Typography::ui(16, true));
        const auto setupSlider = [this](juce::Slider& slider, const juce::String& title,
                                        double minimum, double maximum, double interval, int decimals)
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 26);
            slider.setRange(minimum, maximum, interval);
            slider.setNumDecimalPlacesToDisplay(decimals);
            slider.setTitle(title);
            slider.onDragStart = [this] { taskDragging_ = true; };
            slider.onValueChange = [this, &slider] { if (!taskDragging_) saveTaskSlider(slider); };
            slider.onDragEnd = [this, &slider] { taskDragging_ = false; saveTaskSlider(slider); };
        };
        setupSlider(generatorFrequency_, "Frequency in hertz", 20, 20000, 0.1, 2);
        setupSlider(generatorStart_, "Sweep start frequency in hertz", 20, 20000, 0.1, 2);
        setupSlider(generatorEnd_, "Sweep end frequency in hertz", 20, 20000, 0.1, 2);
        setupSlider(generatorDuration_, "Sweep time in seconds", 0.001, 3600, 0.001, 3);
        setupSlider(generatorLevel_, "Output Level target peak in dBFS", -80, 12, 0.1, 1);
        setupSlider(generatorRoot_, "Root MIDI note", 0, 127, 1, 0);
        setupSlider(generatorStep_, "Musical sweep step time in seconds", 0.01, 10, 0.01, 2);
        generatorGoal_.onChange = [this] { applyGeneratorGoal(generatorGoal_.getSelectedId()); };
        generatorWaveform_.onChange = [this]
        {
            if (type_ == "Signal Generator" && generatorWaveform_.getSelectedId() > 0)
                saveBase(audio::signalWaveform, "audio.param.waveform",
                         generatorWaveform_.getSelectedId() - 1);
            refreshGeneratorVisibility();
        };
        generatorNoise_.onChange = [this]
        {
            if (type_ == "Noise Generator" && generatorNoise_.getSelectedId() > 0)
                saveBase(0, "audio.param.color", generatorNoise_.getSelectedId() - 1);
            else if (type_ == "Signal Generator" && generatorGoal_.getSelectedId() == 4
                     && generatorNoise_.getSelectedId() > 0)
                saveBase(audio::signalWaveform, "audio.param.waveform",
                         generatorNoise_.getSelectedId() + 3);
        };
        generatorScale_.onChange = [this]
        {
            if (type_ == "Signal Generator" && generatorScale_.getSelectedId() > 0)
                saveBase(audio::signalScale, "audio.param.scale", generatorScale_.getSelectedId() - 1);
        };
        generatorCurve_.onChange = [this]
        {
            if (type_ == "Signal Generator" && generatorCurve_.getSelectedId() > 0)
                saveBase(audio::signalSweepCurve, "audio.param.sweepCurve", generatorCurve_.getSelectedId() - 1);
        };
        generatorCalibration_.onChange = [this]
        {
            if (type_ != "Signal Generator" || generatorCalibration_.getSelectedId() == 0) return;
            const auto pinkNoise = generatorCalibration_.getSelectedId() == 2;
            saveBase(audio::signalMode, "audio.param.mode", 0);
            saveBase(audio::signalWaveform, "audio.param.waveform", pinkNoise ? 5 : 0);
            if (!pinkNoise) saveBase(audio::signalFrequency, "audio.param.frequencyHz", 1000);
            status("Digital reference selected. This is not voltage or SPL calibration.");
        };
        advancedToggle_.setButtonText("Advanced");
        automationToggle_.setButtonText("Automation");
        developerToggle_.setButtonText("Developer");
        advancedToggle_.setTitle("Show or hide advanced signal controls");
        automationToggle_.setTitle("Show or hide automation controls");
        developerToggle_.setTitle("Show or hide developer details");
        advancedToggle_.onClick = [this] { advancedExpanded_ = !advancedExpanded_; updateTaskDisclosures(); };
        automationToggle_.onClick = [this] { automationExpanded_ = !automationExpanded_; updateTaskDisclosures(); };
        developerToggle_.onClick = [this] { developerExpanded_ = !developerExpanded_; updateTaskDisclosures(); };
        developerDetails_.setText("Developer details show processor parameter keys and engine ranges. "
                                  "Editing those values still uses the normal validated project and runtime paths.", false);
        developerDetails_.setReadOnly(true);
        developerDetails_.setMultiLine(true, true);
        generatorPeak_.setText(juce::String::fromUTF8("Sample Peak  —  waiting"), juce::dontSendNotification);
        generatorRms_.setText(juce::String::fromUTF8("Measured RMS  —  waiting"), juce::dontSendNotification);
        generatorMeterState_.setText("Waiting for generator samples", juce::dontSendNotification);
        generatorSafety_.setText("Connect to Main Output in the canvas. Start is explicit. dBFS is digital, not voltage or SPL.",
                                 juce::dontSendNotification);
        generatorPeak_.setTitle("Measured sample peak in dBFS over the latest 50 milliseconds");
        generatorRms_.setTitle("Measured RMS in dBFS over a rolling 300 millisecond window");
        generatorMeterState_.setTitle("Generator measurement state");
        generatorSafety_.setTitle("Digital signal and routing safety note");
        generatorPreview_.setName("Generator waveform and sweep preview");
        generatorMeter_.setName("Measured sample peak and RMS level meter");
        for (auto* toggle : {&advancedToggle_, &automationToggle_, &developerToggle_})
            toggle->setClickingTogglesState(true);
        for (auto* label : {&generatorMeterState_, &generatorPeak_, &generatorRms_, &generatorSafety_})
        {
            label->setColour(juce::Label::textColourId, ShellTheme::textPrimary());
        }
        generatorMeterState_.setFont(Typography::ui(15.5f, true));
        generatorPeak_.setFont(Typography::numeric(18));
        generatorRms_.setFont(Typography::numeric(18));
        generatorSafety_.setFont(Typography::ui(15));
    }

    void applyGeneratorGoal(int goal)
    {
        if (type_ != "Signal Generator" || goal < 1 || goal > 5 || !editable()) return;
        const auto* node = current();
        const auto valueFor = [node](const char* key, double fallback)
        {
            if (!node) return fallback;
            const auto found = node->parameters.find(key);
            if (found == node->parameters.end()) return fallback;
            if (const auto* number = std::get_if<double>(&found->second)) return *number;
            return fallback;
        };
        if (goal >= 1 && goal <= 3)
        {
            const auto waveform = static_cast<int>(valueFor("audio.param.waveform", 0));
            if (waveform == 4 || waveform == 5)
                saveBase(audio::signalWaveform, "audio.param.waveform", 0);
            saveBase(audio::signalMode, "audio.param.mode", goal - 1);
        }
        else if (goal == 4)
        {
            saveBase(audio::signalMode, "audio.param.mode", 0);
            saveBase(audio::signalWaveform, "audio.param.waveform",
                     std::clamp(valueFor("audio.param.waveform", 4), 4.0, 5.0));
        }
        else
        {
            generatorCalibration_.setSelectedId(0, juce::dontSendNotification);
            status("Choose a digital reference tone or pink-noise reference. No voltage or SPL calibration is implied.");
            refreshGeneratorVisibility();
            resized();
        }
    }

    void saveTaskSlider(juce::Slider& slider)
    {
        if (type_ == "Signal Generator")
        {
            if (&slider == &generatorFrequency_) saveBase(audio::signalFrequency, "audio.param.frequencyHz", slider.getValue());
            else if (&slider == &generatorStart_) saveBase(audio::signalStartFrequency, "audio.param.startFrequencyHz", slider.getValue());
            else if (&slider == &generatorEnd_) saveBase(audio::signalEndFrequency, "audio.param.endFrequencyHz", slider.getValue());
            else if (&slider == &generatorDuration_) saveBase(audio::signalDuration, "audio.param.durationSeconds", slider.getValue());
            else if (&slider == &generatorLevel_) saveBase(audio::signalLevelDb, "audio.param.levelDb", slider.getValue());
            else if (&slider == &generatorRoot_) saveBase(audio::signalRootMidi, "audio.param.rootMidi", slider.getValue());
            else if (&slider == &generatorStep_) saveBase(audio::signalStepSeconds, "audio.param.stepSeconds", slider.getValue());
        }
        else if (type_ == "Noise Generator" && &slider == &generatorLevel_)
            saveBase(1, "audio.param.levelDbRms", slider.getValue());
    }

    void refreshGeneratorVisibility()
    {
        const auto* node = current();
        const bool generator = valid_ && node
            && (type_ == "Signal Generator" || type_ == "Noise Generator");
        generatorGoalLabel_.setVisible(generator && type_ == "Signal Generator");
        generatorGoal_.setVisible(generator && type_ == "Signal Generator");
        const int goal = generatorGoal_.getSelectedId();
        const bool noiseGoal = type_ == "Noise Generator" || goal == 4;
        const bool sweep = goal == 2;
        const bool musical = goal == 3;
        const bool fixed = !sweep && !musical;
        generatorWaveformLabel_.setVisible(generator && type_ == "Signal Generator" && !noiseGoal);
        generatorWaveform_.setVisible(generatorWaveformLabel_.isVisible());
        generatorNoiseLabel_.setVisible(generator && noiseGoal);
        generatorNoise_.setVisible(generatorNoiseLabel_.isVisible());
        generatorCurveLabel_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorCurve_.setVisible(generatorCurveLabel_.isVisible());
        generatorCalibrationLabel_.setVisible(generator && type_ == "Signal Generator" && goal == 5);
        generatorCalibration_.setVisible(generatorCalibrationLabel_.isVisible());
        generatorFrequencyLabel_.setVisible(generator && type_ == "Signal Generator" && fixed && !noiseGoal);
        generatorFrequency_.setVisible(generator && type_ == "Signal Generator" && fixed && !noiseGoal);
        generatorStartLabel_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorStart_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorEndLabel_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorEnd_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorDurationLabel_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorDuration_.setVisible(generator && type_ == "Signal Generator" && sweep);
        generatorScaleLabel_.setVisible(generator && musical);
        generatorScale_.setVisible(generator && musical);
        generatorRootLabel_.setVisible(generator && musical);
        generatorRoot_.setVisible(generator && musical);
        generatorStepLabel_.setVisible(generator && musical);
        generatorStep_.setVisible(generator && musical);
        generatorLevelLabel_.setVisible(generator);
        generatorLevel_.setVisible(generator);
        generatorPreview_.setVisible(generator);
        generatorMeter_.setVisible(generator);
        generatorPeak_.setVisible(generator);
        generatorRms_.setVisible(generator);
        generatorMeterState_.setVisible(generator);
        generatorMute_.setVisible(generator);
        generatorSafety_.setVisible(generator);
        advancedToggle_.setVisible(generator);
        automationToggle_.setVisible(generator);
        developerToggle_.setVisible(generator);
        developerDetails_.setVisible(generator && developerExpanded_);
        frequencyEntryLabel_.setVisible(generator && type_ == "Signal Generator" && fixed && !noiseGoal);
        frequencyEntry_.setVisible(frequencyEntryLabel_.isVisible());
        frequencyApply_.setVisible(frequencyEntryLabel_.isVisible());
        scaleIntervalsLabel_.setVisible(generator && musical && generatorScale_.getSelectedId() == 6);
        scaleIntervals_.setVisible(scaleIntervalsLabel_.isVisible());
        scaleApply_.setVisible(scaleIntervalsLabel_.isVisible());
        const auto waveform = type_ == "Signal Generator" ? generatorWaveform_.getSelectedId() - 1 : 0;
        const auto mode = type_ == "Signal Generator" && (goal == 2 || goal == 3) ? goal - 1 : 0;
        generatorPreview_.setSignal(mode, waveform, static_cast<float>(generatorStart_.getValue()),
                                    static_cast<float>(generatorEnd_.getValue()),
                                    generatorCurve_.getSelectedId() - 1);
        for (auto& row : rows_)
        {
            row->setSections(!generator || advancedExpanded_, !generator || automationExpanded_);
            row->setVisible(row->preferredHeight() > 0);
        }
        controlLabel_.setVisible(valid_ && controlTarget_.getNumItems() > 0
                                 && (!generator || automationExpanded_));
        controlTarget_.setVisible(controlLabel_.isVisible());
        controlSource_.setVisible(controlLabel_.isVisible());
        controlSave_.setVisible(controlLabel_.isVisible() && controlSource_.getSelectedId() != 1);
        updateControlVisibility();
    }

    void refreshGeneratorMeters()
    {
        if (!owner_.onReadGeneratorMeters || (type_ != "Signal Generator" && type_ != "Noise Generator")
            || selected_ == 0) return;
        const auto meter = owner_.onReadGeneratorMeters(selected_);
        const auto* node = current();
        const auto muted = node && taskValue(*node, "audio.param.muted", 0) >= 0.5;
        const auto prefix = muted ? juce::String::fromUTF8("Muted · ") : juce::String{};
        generatorMeterState_.setText(prefix + meter.state,
                                     juce::dontSendNotification);
        if (type_ == "Noise Generator")
            generatorMeterState_.setText(prefix + meter.state
                                             + juce::String::fromUTF8(" · RMS is primary; noise peaks vary"),
                                         juce::dontSendNotification);
        generatorPeak_.setText("Sample Peak  " + (meter.samplePeakValid
            ? juce::String(meter.samplePeakDbfs, 1) + " dBFS"
            : juce::String::fromUTF8("—")), juce::dontSendNotification);
        generatorRms_.setText("Measured RMS  " + (meter.rmsValid
            ? juce::String(meter.rmsDbfs, 1) + " dBFS"
            : juce::String::fromUTF8("—")), juce::dontSendNotification);
        generatorMeter_.setLevels(meter.samplePeakDbfs, meter.samplePeakValid,
                                  meter.rmsDbfs, meter.rmsValid);
    }
    double taskValue(const domain::Node& node, const char* key, double fallback) const
    {
        const auto found = node.parameters.find(key);
        if (found == node.parameters.end()) return fallback;
        if (const auto* number = std::get_if<double>(&found->second)) return *number;
        return fallback;
    }
    void refreshTaskEditor()
    {
        const auto* node = current();
        const bool signal = valid_ && node && type_ == "Signal Generator";
        const bool noise = valid_ && node && type_ == "Noise Generator";
        if (!signal && !noise)
        {
            refreshGeneratorVisibility();
            return;
        }
        if (signal)
        {
            const auto mode = static_cast<int>(taskValue(*node, "audio.param.mode", 0));
            const auto waveform = static_cast<int>(taskValue(*node, "audio.param.waveform", 0));
            const auto goal = mode == 1 ? 2 : mode == 2 ? 3 : waveform == 4 || waveform == 5 ? 4 : 1;
            generatorGoal_.setSelectedId(goal, juce::dontSendNotification);
            generatorWaveform_.setSelectedId(waveform + 1, juce::dontSendNotification);
            generatorFrequency_.setValue(taskValue(*node, "audio.param.frequencyHz", 440), juce::dontSendNotification);
            generatorStart_.setValue(taskValue(*node, "audio.param.startFrequencyHz", 20), juce::dontSendNotification);
            generatorEnd_.setValue(taskValue(*node, "audio.param.endFrequencyHz", 20000), juce::dontSendNotification);
            generatorDuration_.setValue(taskValue(*node, "audio.param.durationSeconds", 5), juce::dontSendNotification);
            generatorLevel_.setValue(taskValue(*node, "audio.param.levelDb", -60), juce::dontSendNotification);
            generatorScale_.setSelectedId(static_cast<int>(taskValue(*node, "audio.param.scale", 0)) + 1,
                                          juce::dontSendNotification);
            generatorCurve_.setSelectedId(static_cast<int>(taskValue(*node, "audio.param.sweepCurve", 1)) + 1,
                                          juce::dontSendNotification);
            generatorRoot_.setValue(taskValue(*node, "audio.param.rootMidi", 36), juce::dontSendNotification);
            generatorStep_.setValue(taskValue(*node, "audio.param.stepSeconds", 0.5), juce::dontSendNotification);
            generatorNoise_.setSelectedId(waveform >= 4 ? waveform - 3 : 1, juce::dontSendNotification);
        }
        else
        {
            generatorNoise_.setSelectedId(static_cast<int>(taskValue(*node, "audio.param.color", 0)) + 1,
                                          juce::dontSendNotification);
            generatorLevel_.setValue(taskValue(*node, "audio.param.levelDbRms", -60), juce::dontSendNotification);
        }
        generatorMute_.setToggleState(taskValue(*node, "audio.param.muted", 0) >= 0.5,
                                      juce::dontSendNotification);
        refreshGeneratorVisibility();
    }
    void updateTaskDisclosures()
    {
        advancedToggle_.setButtonText(juce::String::fromUTF8(advancedExpanded_ ? "▾ Advanced" : "▸ Advanced"));
        automationToggle_.setButtonText(juce::String::fromUTF8(automationExpanded_ ? "▾ Automation" : "▸ Automation"));
        developerToggle_.setButtonText(juce::String::fromUTF8(developerExpanded_ ? "▾ Developer" : "▸ Developer"));
        advancedToggle_.setToggleState(advancedExpanded_, juce::dontSendNotification);
        automationToggle_.setToggleState(automationExpanded_, juce::dontSendNotification);
        developerToggle_.setToggleState(developerExpanded_, juce::dontSendNotification);
        if (processor_)
        {
            juce::String details = "Node " + juce::String(static_cast<juce::int64>(selected_))
                + juce::String::fromUTF8(" · audio.type = ") + type_ + "\n";
            for (const auto& spec : processor_->parameters())
                details += juce::String(spec.name) + " [" + juce::String(spec.minimum)
                    + ", " + juce::String(spec.maximum) + "]\n";
            developerDetails_.setText(details, false);
        }
        refreshGeneratorVisibility();
        resized();
    }
    void setNode(std::uint64_t id)
    {
        rememberDraft();
        rememberScaleDraft();
        script_ = false;
        frequencyEntry_.clear();
        selected_ = id;
        refresh();
    }
    std::uint64_t node() const noexcept { return selected_; }
    void refresh()
    {
        rememberDraft();
        rememberScaleDraft();
        ++generation_;
        rows_.clear();
        processor_.reset();
        program_.reset();
        controlTarget_.clear(juce::dontSendNotification);
        controlTarget_.setSelectedId(0, juce::dontSendNotification);
        script_ = false;
        valid_ = false;
        patch_ = workspace_.project().activePatch;
        const auto* selected = current();
        title_.setText(selected ? juce::String(selected->name) : "Module editor", juce::dontSendNotification);
        bypass_.setEnabled(false);
        if (!selected)
            status(selected_ == 0 ? "Select an audio module instance (placed node) to edit." : "Selected node no longer exists.");
        else
        {
            const auto field = selected->parameters.find("audio.type");
            const auto* type = field != selected->parameters.end() ? std::get_if<std::string>(&field->second) : nullptr;
            if (!type) status("Structural routing draft, not a module instance or executable pass-through. An explicit audio.type is required for execution. Use Module Browser for audio functions or Input / Output routing utilities.");
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
                if (processor_ && type_ == "Signal Generator")
                    if (const auto custom = selected->parameters.find("audio.scaleIntervals");
                        custom != selected->parameters.end())
                        if (const auto* text = std::get_if<std::string>(&custom->second))
                        {
                            const auto parsed = audio::parseUserScale(*text);
                            if (parsed)
                                if (auto* signal = dynamic_cast<audio::SignalGeneratorNode*>(processor_.get()))
                                    if (!signal->setUserScale({parsed.offsets.data(), parsed.count}))
                                        status("Saved custom scale metadata is invalid for this module.");
                        }

                if (processor_)
                {
                    domain::Graph isolated;
                    isolated.nodes.emplace(selected_, *selected);
                    const auto validation = audio::AudioGraph::fromStructural(isolated, 1);
                    if (!validation.value) status(juce::String(validation.error.message));
                    else
                    {
                        valid_ = true;
                        const bool routing = type_ == "Input" || type_ == "Output" || type_ == "Sum";
                        bypass_.setEnabled(!routing);
                        title_.setText(juce::String(selected->name)
                                           + (routing ? " - routing utility"
                                              : type_.find("Generator") != std::string::npos ? "" : " - module instance"),
                                       juce::dontSendNotification);
                        title_.setFont(type_.find("Generator") != std::string::npos
                                           ? Typography::ui(18, true)
                                           : Typography::font(TextRole::panelTitle));
                        const auto bypass = selected->parameters.find("audio.bypass");
                        bypass_.setToggleState(bypass != selected->parameters.end() && std::get<bool>(bypass->second),
                                               juce::dontSendNotification);
                        addRows(*selected);
                        populateControlTargets();
                        status(script_ ? "Saved source verified. Script INPUT/OUTPUT counts must match the runtime bus; "
                                         "mono examples require a mono bus."
                                       : type_ == "Sum" ? "Explicit unity sum: connect multiple sources to Sum In. Corresponding channels add without normalization or limiting; check levels before Start."
                                       : routing ? "Routing utility: connects the configured device bus, not a reusable effect. No bypass or processing parameters."
                                       : type_ == "Analyzer" ? "Analysis module instance; audio passes through unchanged while shared visualizations observe it."
                                                   : type_ == "DC Generator" ? "Zero-input DC source, exact -1.0 to +1.0. Check connected equipment before Start; values are not an acoustic safety limit."
                                                   : type_.find("Generator") != std::string::npos
                                                       ? "Ready. No audio until Start; check your listening level before raising Output Level."
                                                   : "Module instance: saved base values are shown; live automation/override state is owned by the runtime.");
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
        const bool taskGenerator = valid_ && (type_ == "Signal Generator" || type_ == "Noise Generator");
        bypass_.setVisible(valid_ && !taskGenerator);
        explanation_.setVisible(valid_ && !taskGenerator);
        scheduleLabel_.setVisible(valid_ && !rows_.empty() && (!taskGenerator || automationExpanded_));
        seconds_.setVisible(valid_ && !rows_.empty() && (!taskGenerator || automationExpanded_));
        const auto* selectedNode = current();
        const bool signalGenerator = selectedNode && valid_ && type_ == "Signal Generator";
        frequencyEntryLabel_.setVisible(signalGenerator);
        frequencyEntry_.setVisible(signalGenerator);
        frequencyApply_.setVisible(signalGenerator);
        scaleIntervalsLabel_.setVisible(signalGenerator);
        scaleIntervals_.setVisible(signalGenerator);
        scaleApply_.setVisible(signalGenerator);
        const bool hasControlTarget = valid_ && controlTarget_.getNumItems() > 0;
        controlLabel_.setVisible(hasControlTarget);
        controlTarget_.setVisible(hasControlTarget);
        controlSource_.setVisible(hasControlTarget);
        controlSave_.setVisible(hasControlTarget && controlSource_.getSelectedId() != 1);
        updateControlVisibility();
        if (signalGenerator)
        {
            const auto saved = selectedNode->parameters.find("audio.scaleIntervals");
            const auto* savedText = saved == selectedNode->parameters.end() ? nullptr
                : std::get_if<std::string>(&saved->second);
            std::optional<std::string> savedBase;
            if (savedText) savedBase = *savedText;
            const auto key = std::pair{patch_, selected_};
            const auto draft = scaleDrafts_.find(key);
            const std::string base = savedBase.value_or("0, 2, 4, 5, 7, 9, 11");
            loadingScale_ = true;
            scaleIntervals_.setText(draft != scaleDrafts_.end() && draft->second.base == savedBase
                                        ? draft->second.text : base,
                                    false);
            loadingScale_ = false;
            scaleEditorBoundNode_ = selected_;
            scaleEditorBoundPatch_ = patch_;
        }
        else
        {
            scaleEditorBoundNode_ = 0;
            scaleEditorBoundPatch_ = 0;
        }
        refreshTaskEditor();
        refreshGeneratorMeters();
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
            if (bypass_.isVisible())
            {
                bypass_.setBounds(4, y, width - 8, 28); y += 32;
            }
            if (explanation_.isVisible())
            {
                explanation_.setBounds(4, y, width - 8, 84); y += 88;
            }
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
        if (generatorGoal_.isVisible())
        {
            generatorGoalLabel_.setBounds(4, y, width - 8, 22); y += 22;
            generatorGoal_.setBounds(4, y, width - 8, 30); y += 36;
        }
        if (generatorWaveform_.isVisible())
        {
            generatorWaveformLabel_.setBounds(4, y, width - 8, 22); y += 22;
            generatorWaveform_.setBounds(4, y, width - 8, 30); y += 36;
        }
        if (generatorNoise_.isVisible())
        {
            generatorNoiseLabel_.setBounds(4, y, width - 8, 22); y += 22;
            generatorNoise_.setBounds(4, y, width - 8, 30); y += 36;
        }
        const auto placeTaskSlider = [&](juce::Label& label, juce::Component& control)
        {
            if (!label.isVisible()) return;
            label.setBounds(4, y, width - 8, 21); y += 21;
            control.setBounds(4, y, width - 8, 29); y += 34;
        };
        placeTaskSlider(generatorFrequencyLabel_, generatorFrequency_);
        if (generatorStartLabel_.isVisible() && generatorEndLabel_.isVisible())
        {
            const auto cell = (width - 12) / 2;
            generatorStartLabel_.setBounds(4, y, cell, 21);
            generatorStart_.setBounds(4, y + 21, cell, 29);
            generatorEndLabel_.setBounds(8 + cell, y, cell, 21);
            generatorEnd_.setBounds(8 + cell, y + 21, cell, 29);
            y += 58;
        }
        placeTaskSlider(generatorDurationLabel_, generatorDuration_);
        placeTaskSlider(generatorScaleLabel_, generatorScale_);
        placeTaskSlider(generatorRootLabel_, generatorRoot_);
        placeTaskSlider(generatorStepLabel_, generatorStep_);
        if (generatorCurveLabel_.isVisible())
        {
            generatorCurveLabel_.setBounds(4, y, width - 8, 21); y += 21;
            generatorCurve_.setBounds(4, y, width - 8, 29); y += 34;
        }
        if (generatorCalibrationLabel_.isVisible())
        {
            generatorCalibrationLabel_.setBounds(4, y, width - 8, 21); y += 21;
            generatorCalibration_.setBounds(4, y, width - 8, 29); y += 34;
        }
        placeTaskSlider(generatorLevelLabel_, generatorLevel_);
        if (generatorPreview_.isVisible())
        {
            generatorPreview_.setBounds(4, y, width - 8, 102); y += 108;
            generatorMeter_.setBounds(4, y, width - 8, 44); y += 50;
            generatorMeterState_.setBounds(4, y, width - 8, 24); y += 25;
            if (type_ == "Noise Generator")
            {
                generatorRms_.setBounds(4, y, width - 8, 28);
                generatorPeak_.setBounds(4, y + 29, width - 8, 26);
            }
            else
            {
                generatorPeak_.setBounds(4, y, width - 8, 26);
                generatorRms_.setBounds(4, y + 27, width - 8, 26);
            }
            y += 57;
        }
        if (generatorSafety_.isVisible())
        {
            generatorSafety_.setBounds(4, y, width - 8, 40); y += 44;
        }
        if (generatorMute_.isVisible())
        {
            generatorMute_.setBounds(4, y, width - 8, 28); y += 34;
        }
        if (advancedToggle_.isVisible())
        {
            auto line = juce::Rectangle<int>(4, y, width - 8, 28);
            const auto cell = line.getWidth() / 3;
            advancedToggle_.setBounds(line.removeFromLeft(cell - 2));
            line.removeFromLeft(3);
            automationToggle_.setBounds(line.removeFromLeft(cell - 2));
            line.removeFromLeft(3);
            developerToggle_.setBounds(line); y += 34;
        }
        if (developerDetails_.isVisible())
        {
            developerDetails_.setBounds(4, y, width - 8, 48); y += 52;
        }
        if (frequencyEntry_.isVisible())
        {
            frequencyEntryLabel_.setBounds(4, y, width - 8, 24); y += 24;
            auto line = juce::Rectangle<int>(4, y, width - 8, 28);
            frequencyApply_.setBounds(line.removeFromRight(108));
            frequencyEntry_.setBounds(line); y += 34;
        }
        if (scaleIntervals_.isVisible())
        {
            scaleIntervalsLabel_.setBounds(4, y, width - 8, 24); y += 24;
            auto line = juce::Rectangle<int>(4, y, width - 8, 28);
            scaleApply_.setBounds(line.removeFromRight(108));
            scaleIntervals_.setBounds(line); y += 34;
        }
        if (controlTarget_.isVisible())
        {
            controlLabel_.setBounds(4, y, width - 8, 24); y += 24;
            auto line = juce::Rectangle<int>(4, y, width - 8, 28);
            controlSource_.setBounds(line.removeFromRight(line.getWidth() / 2));
            controlTarget_.setBounds(line); y += 32;
            if (controlSource_.getSelectedId() != 1)
            {
                line = {4, y, width - 8, 26};
                controlOffset_.setBounds(line.removeFromLeft(line.getWidth() / 2 - 2));
                line.removeFromLeft(4);
                controlDepth_.setBounds(line); y += 30;
            }
            if (controlRate_.isVisible())
            {
                controlRate_.setBounds(4, y, width - 8, 26); y += 30;
            }
            if (controlAttack_.isVisible())
            {
                line = {4, y, width - 8, 26};
                const auto cell = line.getWidth() / 3;
                controlAttack_.setBounds(line.removeFromLeft(cell - 2));
                line.removeFromLeft(3);
                controlHold_.setBounds(line.removeFromLeft(cell - 2));
                line.removeFromLeft(3);
                controlRelease_.setBounds(line); y += 30;
            }
            if (controlMinimum_.isVisible())
            {
                line = {4, y, width - 8, 26};
                controlMinimum_.setBounds(line.removeFromLeft(line.getWidth() / 2 - 2));
                line.removeFromLeft(4);
                controlMaximum_.setBounds(line); y += 30;
            }
            if (controlScript_.isVisible())
            {
                controlScript_.setBounds(4, y, width - 8, 120); y += 124;
            }
            if (controlSource_.getSelectedId() != 1)
            {
                controlSave_.setBounds(4, y, width - 8, 28); y += 34;
            }
        }
        if (valid_ && !rows_.empty())
        {
            scheduleLabel_.setBounds(4, y, width - 8, 24); y += 24;
            seconds_.setBounds(4, y, width - 8, 26); y += 32;
        }
        for (const auto& row : rows_)
        {
            const auto height = row->preferredHeight();
            row->setBounds(4, y, width - 8, height);
            y += height + 6;
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
            const auto unit = parameterUnit(spec.name);
            label_.setText(displayParameterName(owner_.type_, spec.name)
                               + (unit.isEmpty() ? "" : " (" + unit + ")")
                               + "  [" + juce::String(spec.minimum) + " .. "
                               + juce::String(spec.maximum) + "]  saved base",
                           juce::dontSendNotification);
            label_.setTooltip("Physical units and smoothing are defined by the processor; ramp duration "
                              + juce::String(spec.smoothingSeconds * 1000.0) + " ms.");
            label_.setFont(Typography::font(TextRole::label));
            base_.setRange(spec.minimum, spec.maximum,
                           spec.name == std::string_view("rootMidi") || spec.name == std::string_view("seed") ? 1.0 : 0.0);
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
            const auto choices = parameterChoices(owner_.type_, spec.name);
            if (!choices.empty())
            {
                discreteMode_ = true;
                discrete_.setTitle(juce::String(spec.name) + " saved base choice");
                for (std::size_t i = 0; i < choices.size(); ++i)
                    discrete_.addItem(choices[i].first, static_cast<int>(i) + 1);
                const auto found = std::find_if(choices.begin(), choices.end(),
                    [saved](const auto& choice) { return choice.second == static_cast<float>(saved); });
                if (found != choices.end())
                    discrete_.setSelectedId(static_cast<int>(found - choices.begin()) + 1, juce::dontSendNotification);
                discrete_.onChange = [this, choices]
                {
                    const auto selected = discrete_.getSelectedId() - 1;
                    if (selected >= 0 && selected < static_cast<int>(choices.size()))
                        owner_.deferSaveBase(index_, key_, choices[static_cast<std::size_t>(selected)].second);
                };
            }
            targetLabel_.setText("Live target (not saved)", juce::dontSendNotification);
            target_.setText(juce::String(saved, 6), false);
            target_.setFont(Typography::font(TextRole::value));
            target_.setTitle(juce::String(spec.name) + " live override or automation target");
            override_.onClick = [this] { apply(audio::ParameterAction::overrideValue, false); };
            release_.onClick = [this] { owner_.runtime(index_, 0, audio::ParameterAction::releaseOverride); };
            automation_.onClick = [this] { apply(audio::ParameterAction::automation, false); };
            disable_.onClick = [this] { owner_.runtime(index_, 0, audio::ParameterAction::disableAutomation); };
            schedule_.onClick = [this] { apply(audio::ParameterAction::automation, true); };
            for (auto* child : std::initializer_list<juce::Component*>{
                     &label_, &base_, &discrete_, &targetLabel_, &target_, &override_, &release_, &automation_, &disable_, &schedule_})
                addAndMakeVisible(child);
            discrete_.setVisible(discreteMode_);
            base_.setVisible(!discreteMode_);
        }
        void setSections(bool advanced, bool automation)
        {
            advanced_ = advanced;
            automationSection_ = automation;
            label_.setVisible(advanced_);
            base_.setVisible(advanced_ && !discreteMode_);
            discrete_.setVisible(advanced_ && discreteMode_);
            targetLabel_.setVisible(automationSection_);
            target_.setVisible(automationSection_);
            override_.setVisible(automationSection_);
            release_.setVisible(automationSection_);
            automation_.setVisible(automationSection_);
            disable_.setVisible(automationSection_);
            schedule_.setVisible(automationSection_);
        }
        int preferredHeight() const noexcept
        { return (advanced_ ? 54 : 0) + (automationSection_ ? 116 : 0); }
        void resized() override
        {
            int y = 0;
            if (advanced_)
            {
                label_.setBounds(0, y, getWidth(), 24); y += 24;
                const auto baseBounds = juce::Rectangle<int>(0, y, getWidth(), 30);
                base_.setBounds(baseBounds);
                discrete_.setBounds(baseBounds); y += 30;
            }
            if (!automationSection_) return;
            auto bounds = juce::Rectangle<int>(0, y, getWidth(), getHeight() - y);
            auto targetLine = bounds.removeFromTop(26);
            target_.setBounds(targetLine.removeFromRight(std::min(110, targetLine.getWidth() / 2)));
            targetLabel_.setBounds(targetLine);
            bounds.removeFromTop(3);
            auto line = bounds.removeFromTop(24);
            override_.setBounds(line.removeFromLeft(line.getWidth() / 2).reduced(1, 0));
            release_.setBounds(line.reduced(1, 0));
            bounds.removeFromTop(3);
            line = bounds.removeFromTop(24);
            automation_.setBounds(line.removeFromLeft(line.getWidth() / 2).reduced(1, 0));
            disable_.setBounds(line.reduced(1, 0));
            bounds.removeFromTop(3);
            schedule_.setBounds(bounds.removeFromTop(22));
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
        bool dragging_{}, discreteMode_{}, advanced_{true}, automationSection_{true};
        juce::Label label_, targetLabel_;
        juce::Slider base_;
        juce::ComboBox discrete_;
        juce::TextEditor target_;
        juce::TextButton override_{"Apply override"}, release_{"Release override"},
            automation_{"Automation now"}, disable_{"Disable automation"}, schedule_{"Schedule automation target"};
    };

    struct Draft
    {
        std::optional<std::string> base;
        std::string text;
    };

    void rememberScaleDraft()
    {
        if (scaleEditorBoundNode_ == 0 || scaleEditorBoundNode_ != selected_
            || scaleEditorBoundPatch_ != patch_)
            return;
        const auto* selected = current();
        if (!selected) return;
        const auto saved = selected->parameters.find("audio.scaleIntervals");
        const auto* savedText = saved == selected->parameters.end() ? nullptr
            : std::get_if<std::string>(&saved->second);
        std::optional<std::string> base;
        if (savedText) base = *savedText;
        const auto text = scaleIntervals_.getText().trim().toStdString();
        const auto defaultText = std::string("0, 2, 4, 5, 7, 9, 11");
        if (text == base.value_or(defaultText))
        {
            scaleDrafts_.erase({patch_, selected_});
            return;
        }
        scaleDrafts_.insert_or_assign({patch_, selected_}, Draft{std::move(base), text});
    }

    void saveFrequencyEntry()
    {
        if (!editable() || type_ != "Signal Generator") return;
        double referenceA = 440;
        if (const auto* selected = current())
            if (const auto found = selected->parameters.find("audio.param.referenceAHz");
                found != selected->parameters.end())
                if (const auto* value = std::get_if<double>(&found->second)) referenceA = *value;
        const auto parsed = audio::parseFrequencyEntry(frequencyEntry_.getText().toStdString(), referenceA);
        if (!parsed)
        {
            status(juce::String(parsed.error.data(), static_cast<int>(parsed.error.size())));
            return;
        }
        saveBase(audio::signalFrequency, "audio.param.frequencyHz", parsed.hertz);
    }

    void saveUserScale()
    {
        if (!editable() || type_ != "Signal Generator") return;
        const auto parsed = audio::parseUserScale(scaleIntervals_.getText().toStdString());
        if (!parsed)
        {
            status(juce::String(parsed.error.data(), static_cast<int>(parsed.error.size())));
            return;
        }
        int root = 36;
        if (const auto* selected = current())
            if (const auto found = selected->parameters.find("audio.param.rootMidi");
                found != selected->parameters.end())
                if (const auto* value = std::get_if<double>(&found->second)) root = static_cast<int>(*value);
        if (root + parsed.offsets[parsed.count - 1] > 127)
        {
            status("The largest scale degree plus the saved root would exceed MIDI note 127.");
            return;
        }
        const auto saved = workspace_.setParameter(selected_, "audio.scaleIntervals",
                                                     scaleIntervals_.getText().trim().toStdString());
        if (!saved.succeeded) { status(juce::String(saved.error)); return; }
        scaleDrafts_.erase({patch_, selected_});
        status("Custom scale saved. Musical Sweep uses its ascending semitone offsets from the selected root.");
        if (owner_.onChanged) owner_.onChanged();
    }

    void populateControlTargets()
    {
        if (!processor_) return;
        const auto parameters = processor_->parameters();
        for (std::size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i].smoothingSeconds > 0)
                controlTarget_.addItem(juce::String(parameters[i].name), static_cast<int>(i) + 1);
        if (controlTarget_.getNumItems() > 0)
        {
            controlTarget_.setSelectedId(controlTarget_.getItemId(0), juce::dontSendNotification);
            loadControlEditor();
        }
    }

    void loadControlEditor()
    {
        const auto* node = current();
        if (!node || !processor_) return;
        const auto index = controlTarget_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(processor_->parameters().size())) return;
        const auto& parameter = processor_->parameters()[static_cast<std::size_t>(index)];
        const std::string prefix = "audio.control." + std::string(parameter.name) + ".";
        const auto getText = [&](std::string_view field, std::string fallback)
        {
            const auto found = node->parameters.find(prefix + std::string(field));
            const auto* value = found == node->parameters.end() ? nullptr
                : std::get_if<std::string>(&found->second);
            return value ? *value : std::move(fallback);
        };
        const auto getNumber = [&](std::string_view field, double fallback)
        {
            const auto found = node->parameters.find(prefix + std::string(field));
            const auto* value = found == node->parameters.end() ? nullptr
                : std::get_if<double>(&found->second);
            return value ? *value : fallback;
        };
        const auto source = getText("source", "none");
        const auto sourceId = source == "lfo" ? 2 : source == "envelope" ? 3 : source == "script" ? 4 : 1;
        controlSource_.setSelectedId(sourceId, juce::dontSendNotification);
        controlOffset_.setText(juce::String(getNumber("offset", 0), 6), false);
        controlDepth_.setText(juce::String(getNumber("depth", 0), 6), false);
        controlRate_.setText(juce::String(getNumber("rateHz", 1), 6), false);
        controlAttack_.setText(juce::String(getNumber("attackSeconds", 0.01), 6), false);
        controlHold_.setText(juce::String(getNumber("holdSeconds", 0), 6), false);
        controlRelease_.setText(juce::String(getNumber("releaseSeconds", 0.5), 6), false);
        controlMinimum_.setText(juce::String(getNumber("minimum", 0), 6), false);
        controlMaximum_.setText(juce::String(getNumber("maximum", 1), 6), false);
        controlScript_.setText(getText("script",
            "NODE ScalarControl AT AUDIO;\nINPUT Time: REAL;\nOUTPUT Value: REAL;\nBEGIN\n  Value := Time * 0.1;\nEND."), false);
        updateControlVisibility();
    }

    void updateControlVisibility()
    {
        const auto source = controlSource_.getSelectedId();
        const bool active = controlTarget_.isVisible() && source != 1 && source != 0;
        controlOffset_.setVisible(active);
        controlDepth_.setVisible(active);
        controlRate_.setVisible(active && source == 2);
        controlAttack_.setVisible(active && source == 3);
        controlHold_.setVisible(active && source == 3);
        controlRelease_.setVisible(active && source == 3);
        controlMinimum_.setVisible(active && source == 4);
        controlMaximum_.setVisible(active && source == 4);
        controlScript_.setVisible(active && source == 4);
        controlSave_.setVisible(active);
        resized();
    }

    void saveControlEditor()
    {
        if (!editable() || !processor_) return;
        const auto selectedIndex = controlTarget_.getSelectedId() - 1;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(processor_->parameters().size()))
        {
            status("Choose a supported continuous parameter before saving its control mapping.");
            return;
        }
        const auto sourceId = controlSource_.getSelectedId();
        const std::string source = sourceId == 2 ? "lfo" : sourceId == 3 ? "envelope"
            : sourceId == 4 ? "script" : "none";
        const auto& parameter = processor_->parameters()[static_cast<std::size_t>(selectedIndex)];
        const std::string prefix = "audio.control." + std::string(parameter.name) + ".";
        std::map<std::string, domain::Parameter> values;
        values.emplace(prefix + "source", source);
        const auto addNumber = [&](std::string_view field, const juce::TextEditor& editor) -> bool
        {
            double number{};
            if (!parseNumber(editor.getText(), number)) return false;
            values.emplace(prefix + std::string(field), number);
            return true;
        };
        if (source != "none")
        {
            if (!addNumber("offset", controlOffset_) || !addNumber("depth", controlDepth_))
            {
                status("Control offset and depth must be finite numbers.");
                return;
            }
            if (source == "lfo" && !addNumber("rateHz", controlRate_))
            {
                status("LFO rate must be a finite value from 0.01 to 20 Hz.");
                return;
            }
            if (source == "envelope"
                && (!addNumber("attackSeconds", controlAttack_) || !addNumber("holdSeconds", controlHold_)
                    || !addNumber("releaseSeconds", controlRelease_)))
            {
                status("Envelope attack, hold and release must be finite seconds.");
                return;
            }
            if (source == "script")
            {
                if (!addNumber("minimum", controlMinimum_) || !addNumber("maximum", controlMaximum_))
                {
                    status("Script output minimum and maximum must be finite numbers.");
                    return;
                }
                values.emplace(prefix + "script", controlScript_.getText().toStdString());
            }
        }
        auto replacement = *current();
        for (const auto& [key, value] : values)
            replacement.parameters.insert_or_assign(key, value);
        domain::Graph isolated;
        isolated.nodes.emplace(selected_, std::move(replacement));
        const auto validation = audio::AudioGraph::fromStructural(isolated, 1);
        if (!validation.value)
        {
            status("Control mapping not saved: " + juce::String(validation.error.message));
            return;
        }
        const auto callback = owner_.onChanged;
        const auto saved = workspace_.setParameters(selected_, std::move(values));
        if (!saved.succeeded) { status(juce::String(saved.error)); return; }
        status(source == "none" ? "Scalar control mapping disabled."
                                : "Typed scalar control mapping saved; it starts with explicit transport and follows prepared sample time.");
        if (callback) callback();
    }

    /** A node-local draft retains the saved baseline so external source changes are visible. */
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
        if (!std::isfinite(value) || value < spec.minimum || value > spec.maximum
            || !processor_->accepts(parameter, static_cast<float>(value), audio::ParameterAction::base))
        {
            status("Saved base must be finite and within the displayed parameter range.");
            return;
        }
        auto replacement = *current();
        replacement.parameters.insert_or_assign(key, value);
        domain::Graph isolated;
        isolated.nodes.emplace(selected_, std::move(replacement));
        const auto graphValidation = audio::AudioGraph::fromStructural(isolated, 1);
        if (!graphValidation.value)
        {
            status("Saved base not changed: " + juce::String(graphValidation.error.message));
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
    std::uint64_t scaleEditorBoundNode_{}, scaleEditorBoundPatch_{};
    std::string type_;
    std::optional<std::string> savedSource_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Draft> drafts_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Draft> scaleDrafts_;
    bool valid_{}, script_{}, loadingSource_{}, loadingScale_{}, taskDragging_{};
    bool advancedExpanded_{}, automationExpanded_{}, developerExpanded_{};
    juce::Component content_;
    juce::Viewport viewport_;
    juce::TextEditor status_, source_, seconds_;
    juce::TextEditor frequencyEntry_, scaleIntervals_, controlOffset_, controlDepth_, controlRate_;
    juce::TextEditor controlAttack_, controlHold_, controlRelease_, controlMinimum_, controlMaximum_, controlScript_;
    juce::Label title_, explanation_, scheduleLabel_, sourceLabel_, frequencyEntryLabel_, scaleIntervalsLabel_, controlLabel_;
    juce::Label generatorGoalLabel_, generatorWaveformLabel_, generatorFrequencyLabel_, generatorStartLabel_,
        generatorEndLabel_, generatorDurationLabel_, generatorLevelLabel_, generatorNoiseLabel_,
        generatorScaleLabel_, generatorRootLabel_, generatorStepLabel_, generatorCurveLabel_,
        generatorCalibrationLabel_, generatorPeak_, generatorRms_,
        generatorMeterState_, generatorSafety_;
    juce::TextEditor developerDetails_;
    juce::ToggleButton bypass_{"Saved bypass (dry for effects; silence for sources)"};
    juce::ToggleButton generatorMute_{"Mute output"};
    juce::ComboBox examples_, controlTarget_, controlSource_, generatorGoal_, generatorWaveform_,
        generatorNoise_, generatorScale_, generatorCurve_, generatorCalibration_;
    juce::TextButton compile_{"Compile / Apply"}, load_{"Load Source..."}, revert_{"Revert to saved source"},
        frequencyApply_{"Set Frequency"}, scaleApply_{"Apply scale"}, controlSave_{"Save typed control mapping"},
        advancedToggle_, automationToggle_, developerToggle_;
    juce::Slider generatorFrequency_, generatorStart_, generatorEnd_, generatorDuration_, generatorLevel_,
        generatorRoot_, generatorStep_;
    GeneratorPreview generatorPreview_;
    GeneratorMeter generatorMeter_;
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
