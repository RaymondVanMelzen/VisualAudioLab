#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
enum class TextRole
{
    applicationTitle, panelTitle, sectionTitle, nodeTitle, label, value, unit,
    meterScale, meterReadout, code, diagnostics, body, status, tooltip, menu
};
enum class TextDetail { glyph, overview, compact, full };

/** Message-thread font resources and logical-pixel roles; never used by the audio callback. */
class Typography final
{
public:
    [[nodiscard]] static juce::Font font(TextRole role);
    [[nodiscard]] static juce::Font ui(float height, bool semibold = false);
    [[nodiscard]] static juce::Font numeric(float height = 13.0f);
    [[nodiscard]] static juce::Font code(float height = 14.0f, bool bold = false, bool italic = false);
    [[nodiscard]] static bool bundledFontsAvailable();
    [[nodiscard]] static bool tabularFiguresAvailable();
    [[nodiscard]] static TextDetail detail(float zoom) noexcept;
    [[nodiscard]] static float canvasHeight(float logicalHeight, float zoom, float minimumOnScreen) noexcept;
    /** Display-only, locale decimal separator. Never feed these strings to the compiler/codec. */
    [[nodiscard]] static juce::String number(double value, int decimals);
    [[nodiscard]] static juce::String frequency(double hz);
    [[nodiscard]] static juce::String sampleRate(double hz);
    [[nodiscard]] static juce::String level(double db);
    [[nodiscard]] static juce::String quantity(double value, int decimals, const juce::String& unit);
    static void drawQuantity(juce::Graphics&, double value, int decimals, const juce::String& unit,
                             juce::Rectangle<float> bounds, float height = 13.0f);
};
}
