#include "TestSupport.h"
#include <val/presentation/Typography.h>
#include <val/presentation/MainComponent.h>
#include <val/presentation/NodeView.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

namespace
{
using namespace val::presentation;
void save(const juce::Image& image, const juce::File& destination)
{
    auto stream = destination.createOutputStream();
    require(stream != nullptr && !stream->failedToOpen(), "Open screenshot destination.");
    require(stream->setPosition(0) && stream->truncate().wasOk(), "Replace previous evidence rather than append a second PNG.");
    require(juce::PNGImageFormat{}.writeImageToStream(image, *stream), "Encode actual JUCE-rendered screenshot.");
}
void checkFonts()
{
    require(Typography::bundledFontsAvailable(), "Every embedded variable/static UI/code face loads without a font installation.");
    require(Typography::tabularFiguresAvailable(), "Inter actually advertises tnum and zero.");
    require(Typography::numeric().getTypefaceName().contains("Inter"), "Numeric values use bundled Inter, not monospace.");
    require(Typography::code().getTypefaceName().contains("JetBrains"), "Source uses bundled JetBrains Mono.");
    require(Typography::code(13, true).getTypefacePtr()->getStyle().containsIgnoreCase("bold"), "Bold is an actual bundled face.");
    require(Typography::code(13, false, true).getTypefacePtr()->getStyle().containsIgnoreCase("italic"), "Italic is an actual bundled face.");
    require(Typography::font(TextRole::body).getHeight() >= 15
            && Typography::font(TextRole::panelTitle).getHeight() >= 15
            && Typography::font(TextRole::menu).getHeight() >= 14
            && Typography::font(TextRole::status).getHeight() >= 14
            && Typography::font(TextRole::label).getHeight() >= 13,
            "The revised product type ramp uses comfortably readable body, heading and essential-detail sizes.");
    const auto width = [](const juce::Font& font, const char* text) { return juce::GlyphArrangement::getStringWidth(font, text); };
    require(std::abs(width(Typography::numeric(), "111111") - width(Typography::numeric(), "888888")) < 0.01f,
            "Shaped tabular digits have identical advances (not just feature metadata).");
    require(std::abs(width(Typography::code(), "iiiiii") - width(Typography::code(), "WWWWWW")) < 0.01f, "Code is genuinely monospaced.");
    const auto code = Typography::code();
    const auto features = code.getFeatureSettings();
    require(std::any_of(features.begin(), features.end(), [](const auto& feature)
        { return feature.tag == juce::FontFeatureTag{"liga"} && feature.value == 0; }), "Code ligatures are explicitly disabled.");
    const auto nbsp = juce::String::charToString(0xa0);
    require(Typography::frequency(999) == "999" + nbsp + "Hz"
            && Typography::frequency(1000) == Typography::number(1, 2) + nbsp + "kHz", "Frequency switches SI prefix at exact threshold.");
    require(Typography::sampleRate(48000) == "48" + nbsp + "kHz"
            && Typography::sampleRate(44100) == Typography::number(44.1, 1) + nbsp + "kHz", "Sample rates retain useful precision.");
    require(Typography::level(-18.4).startsWithChar(0x2212)
            && Typography::level(-INFINITY).containsChar(0x221e), "Levels use true minus and negative infinity.");
    require(Typography::number(std::numeric_limits<double>::quiet_NaN(), 1) == juce::String::charToString(0x2014),
            "Unavailable values are never NaN or fabricated zero.");
    const auto readout = [](double value)
    {
        juce::Image image(juce::Image::ARGB, 220, 30, true);
        {
            juce::Graphics g(image);
            Typography::drawQuantity(g, value, 1, "dB", image.getBounds().toFloat(), 13);
        }
        return image;
    };
    const auto shortValue = readout(-9.8), longValue = readout(-18.4);
    bool visibleUnit{};
    for (int x = 205; x < 220; ++x)
        for (int y = 0; y < 30; ++y)
        {
            require(shortValue.getPixelAt(x, y) == longValue.getPixelAt(x, y),
                    "Changing numeric digit count never shifts the neighbouring unit.");
            visibleUnit = visibleUnit || !shortValue.getPixelAt(x, y).isTransparent();
        }
    require(visibleUnit, "The stable unit region contains actual rendered text.");
    require(Typography::detail(0.349f) == TextDetail::glyph && Typography::detail(0.35f) == TextDetail::overview
            && Typography::detail(0.6f) == TextDetail::overview && Typography::detail(0.8f) == TextDetail::compact
            && Typography::detail(1.0f) == TextDetail::compact && Typography::detail(1.1f) == TextDetail::full,
            "Readable semantic zoom tiers hide small detail before it becomes too small to read.");
    for (float zoom : {0.1f, 0.35f, 0.59f, 0.6f, 0.99f, 1.0f, 1.5f, 3.0f})
        require(Typography::canvasHeight(12, zoom, 11) * zoom >= 10.999f
                && Typography::canvasHeight(12, zoom, 11) * zoom <= 15.001f, "Canvas text respects screen floor and 1.25x ceiling.");
}
void screenshots(const juce::File& directory)
{
    require(directory.createDirectory().wasOk(), "Create evidence directory.");
    val::application::WorkspaceService workspace;
    for (auto item : {std::pair{"Input", val::domain::Position{-600, 0}}, {"DC Generator", {-500, 140}},
                      {"Sum", {0, 70}}, {"Output", {900, 70}}, {"Delay", {0, 230}}})
        require(workspace.createNode(item.second, item.first, {{"audio.type", std::string(item.first)}}).succeeded, "Create review graph.");
    std::vector<val::domain::GraphId> ids;
    for (const auto& [id, node] : workspace.graph().nodes) { juce::ignoreUnused(node); ids.push_back(id); }
    using val::domain::Port;
    require(workspace.connect({ids[0], Port::output}, {ids[2], Port::input}).succeeded
            && workspace.connect({ids[1], Port::output}, {ids[2], Port::input}).succeeded
            && workspace.connect({ids[2], Port::output}, {ids[3], Port::input}).succeeded, "Review graph is executable and includes inert Delay.");
    MainComponent shell(workspace); // Empty backend factory: no native discovery or playback.
    shell.setSize(1000, 700);
    shell.dockWorkspace().canvas().fitGraph();
    save(shell.createComponentSnapshot(shell.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{}),
         directory.getChildFile("shell-default.png"));
    shell.setSize(800, 600);
    shell.dockWorkspace().canvas().fitGraph();
    save(shell.createComponentSnapshot(shell.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{}),
         directory.getChildFile("shell-compact.png"));
    shell.setSize(1440, 900);
    shell.dockWorkspace().canvas().fitGraph();
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f})
        save(shell.createComponentSnapshot(shell.getLocalBounds(), true, scale, juce::SoftwareImageType{}),
             directory.getChildFile("shell-" + juce::String(scale * 100, 0) + ".png"));
    ShellTheme theme;
    juce::AlertWindow dialog("Explicit output", "Select the device and named channel group before Start. No fallback.",
                             juce::MessageBoxIconType::WarningIcon);
    dialog.setLookAndFeel(&theme);
    dialog.addButton("Keep stopped", 0);
    dialog.setSize(500, 170);
    save(dialog.createComponentSnapshot(dialog.getLocalBounds(), true, 1.5f, juce::SoftwareImageType{}), directory.getChildFile("dialog-150.png"));
    dialog.setLookAndFeel(nullptr);
    ModuleEditor editor(workspace);
    editor.setLookAndFeel(&theme);
    require(workspace.createNode({0, 0}, "Readable script", {{"audio.type", std::string("Script")},
                {"audio.script", ModuleEditor::defaultScriptSource()}}).succeeded, "Create code review fixture.");
    editor.setNode(workspace.lastCreatedNode());
    WorkspacePanel editorPanel(PanelId::propertyInspector, {}, &editor);
    editorPanel.setLookAndFeel(&theme);
    editorPanel.setSize(640, 800);
    save(editorPanel.createComponentSnapshot(editorPanel.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{}), directory.getChildFile("script-editor.png"));
    editorPanel.setLookAndFeel(nullptr);
    editor.setLookAndFeel(nullptr);
    juce::Image image(juce::Image::RGB, 1000, 560, true, juce::SoftwareImageType{});
    {
        juce::Graphics g(image);
        g.fillAll(ShellTheme::background());
        const float zooms[]{0.25f, 0.35f, 0.6f, 1.0f, 1.5f};
        for (int i = 0; i < 5; ++i)
        {
            const auto zoom = zooms[i];
            g.setColour(ShellTheme::textPrimary());
            g.setFont(Typography::ui(12));
            g.drawText("Canvas " + juce::String(zoom * 100, 0) + "%", 20, i * 105 + 8, 150, 25, juce::Justification::centredLeft);
            juce::Graphics::ScopedSaveState saved(g);
            g.addTransform(juce::AffineTransform::scale(zoom).translated(260, static_cast<float>(i * 105 + 8)));
            NodeView::paint(g, workspace.graph().nodes.at(ids[2]), {0, 0}, false, false, zoom);
        }
        require(image.getPixelAt(999, 559) == ShellTheme::background(), "Zoom evidence contains the painted background.");
    }
    save(image, directory.getChildFile("canvas-zoom-tiers.png"));
}
}
void runTypographyTests(const char* directory)
{
    checkFonts();
    if (directory != nullptr) screenshots(juce::File(juce::String::fromUTF8(directory)));
}
