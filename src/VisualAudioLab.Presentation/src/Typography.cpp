#include <val/presentation/Typography.h>
#include <val/presentation/ShellTheme.h>
#include <ValResources.h>
#include <algorithm>
#include <cmath>
#include <locale>

namespace val::presentation
{
namespace
{
juce::Typeface::Ptr load(const char* data, int bytes)
{
    return juce::Typeface::createSystemTypefaceFor(data, static_cast<std::size_t>(bytes));
}
struct Faces
{
    juce::Typeface::Ptr variable = load(val_resources::InterVariable_ttf, val_resources::InterVariable_ttfSize);
    juce::Typeface::Ptr regular = load(val_resources::InterRegular_ttf, val_resources::InterRegular_ttfSize);
    juce::Typeface::Ptr medium = load(val_resources::InterMedium_ttf, val_resources::InterMedium_ttfSize);
    juce::Typeface::Ptr semibold = load(val_resources::InterSemiBold_ttf, val_resources::InterSemiBold_ttfSize);
    juce::Typeface::Ptr bold = load(val_resources::InterBold_ttf, val_resources::InterBold_ttfSize);
    juce::Typeface::Ptr mono = load(val_resources::JetBrainsMonoRegular_ttf, val_resources::JetBrainsMonoRegular_ttfSize);
    juce::Typeface::Ptr monoBold = load(val_resources::JetBrainsMonoBold_ttf, val_resources::JetBrainsMonoBold_ttfSize);
    juce::Typeface::Ptr monoItalic = load(val_resources::JetBrainsMonoItalic_ttf, val_resources::JetBrainsMonoItalic_ttfSize);
};
const Faces& faces() { static const Faces value; return value; }
juce::String fallback(bool mono)
{
    const auto installed = juce::Font::findAllTypefaceNames();
#if JUCE_WINDOWS
    const juce::StringArray candidates = mono ? juce::StringArray{"Cascadia Mono", "Consolas"}
        : juce::StringArray{"Segoe UI Variable", "Segoe UI", "Roboto", "Arial"};
#else
    const juce::StringArray candidates = mono ? juce::StringArray{"SF Mono", "Menlo"}
        : juce::StringArray{"SF Pro Text", "Helvetica Neue", "Roboto", "Arial"};
#endif
    for (const auto& candidate : candidates)
        if (installed.contains(candidate)) return candidate;
    return mono ? juce::Font::getDefaultMonospacedFontName() : juce::Font::getDefaultSansSerifFontName();
}
juce::FontOptions options(juce::Typeface::Ptr face, float height, bool mono = false)
{
    auto result = juce::FontOptions(height);
    return face != nullptr ? result.withTypeface(face) : result.withName(fallback(mono));
}
juce::String unavailable() { return juce::String::charToString(0x2014); }
}

juce::Font Typography::ui(float height, bool semibold)
{
    return juce::Font(options(semibold ? faces().semibold : faces().regular, std::max(13.0f, height)));
}
juce::Font Typography::numeric(float height)
{
    return juce::Font(options(faces().semibold, std::max(14.0f, height))
        .withFeatureEnabled("tnum").withFeatureEnabled("zero"));
}
juce::Font Typography::code(float height, bool bold, bool italic)
{
    return juce::Font(options(italic ? faces().monoItalic : bold ? faces().monoBold : faces().mono,
                              std::max(13.0f, height), true).withFeatureDisabled("liga").withFeatureDisabled("calt"));
}
juce::Font Typography::font(TextRole role)
{
    switch (role)
    {
        case TextRole::applicationTitle: return ui(17, true);
        case TextRole::panelTitle: case TextRole::nodeTitle: return ui(15, true);
        case TextRole::sectionTitle:
            return juce::Font(options(faces().semibold, 14).withFeatureEnabled("case")).withExtraKerningFactor(0.04f);
        case TextRole::value: return numeric(16);
        case TextRole::meterReadout: return numeric(18);
        case TextRole::meterScale:
            return juce::Font(options(faces().medium, 13).withFeatureEnabled("tnum"));
        case TextRole::code: return code(14);
        case TextRole::diagnostics: return code(13.5f);
        case TextRole::body: return ui(15);
        case TextRole::status:
            return juce::Font(options(faces().medium, 14).withFeatureEnabled("tnum").withFeatureEnabled("zero"));
        case TextRole::tooltip: return ui(14);
        case TextRole::menu: return ui(14);
        case TextRole::label: case TextRole::unit: return juce::Font(options(faces().medium, 13.5f));
    }
    return ui(14);
}
bool Typography::bundledFontsAvailable()
{
    const auto& f = faces();
    return f.variable && f.regular && f.medium && f.semibold && f.bold && f.mono && f.monoBold && f.monoItalic;
}
bool Typography::tabularFiguresAvailable()
{
    const auto supported = numeric().getTypefacePtr()->getSupportedFeatures();
    return std::find(supported.begin(), supported.end(), juce::FontFeatureTag{"tnum"}) != supported.end()
        && std::find(supported.begin(), supported.end(), juce::FontFeatureTag{"zero"}) != supported.end();
}
TextDetail Typography::detail(float zoom) noexcept
{
    return zoom < 0.35f ? TextDetail::glyph : zoom < 0.8f ? TextDetail::overview
        : zoom < 1.1f ? TextDetail::compact : TextDetail::full;
}
float Typography::canvasHeight(float logical, float zoom, float minimum) noexcept
{
    if (!std::isfinite(zoom) || zoom <= 0) return logical;
    return std::max(minimum, logical * std::min(zoom, 1.25f)) / zoom;
}
juce::String Typography::number(double value, int decimals)
{
    if (!std::isfinite(value)) return unavailable();
    auto text = juce::String(value == 0 ? 0.0 : value, std::clamp(decimals, 0, 9));
    // The environment locale follows the OS; the classic C++ locale does not.
    static const std::locale locale("");
    const auto separator = std::use_facet<std::numpunct<char>>(locale).decimal_point();
    if (separator != '.') text = text.replaceCharacter('.', static_cast<juce::juce_wchar>(separator));
    return text.replaceCharacter('-', 0x2212);
}
juce::String Typography::quantity(double value, int decimals, const juce::String& unit)
{
    return !std::isfinite(value) ? unavailable()
        : number(value, decimals) + juce::String::charToString(0x00a0) + unit;
}
juce::String Typography::frequency(double hz)
{
    if (!std::isfinite(hz) || hz < 0) return unavailable();
    return hz >= 1000 ? quantity(hz / 1000, 2, "kHz") : quantity(hz, 0, "Hz");
}
juce::String Typography::sampleRate(double hz)
{
    if (!std::isfinite(hz) || hz <= 0) return unavailable();
    return quantity(hz / 1000, std::fmod(hz, 1000) == 0 ? 0 : 1, "kHz");
}
juce::String Typography::level(double db)
{
    if (db == -INFINITY) return juce::String::charToString(0x2212) + juce::String::charToString(0x221e)
        + juce::String::charToString(0x00a0) + "dB";
    return quantity(db, 1, "dB");
}
void Typography::drawQuantity(juce::Graphics& g, double value, int decimals, const juce::String& unit,
                              juce::Rectangle<float> bounds, float height)
{
    const auto valueFont = numeric(height);
    const auto valueText = value == -INFINITY
        ? juce::String::charToString(0x2212) + juce::String::charToString(0x221e) : number(value, decimals);
    const auto unitFont = ui(std::max(10.0f, height * 0.8f));
    const auto unitWidth = juce::GlyphArrangement::getStringWidth(unitFont, unit);
    const auto valueRight = static_cast<int>(std::round(bounds.getRight() - unitWidth - 4.0f));
    g.setFont(valueFont);
    g.setColour(ShellTheme::textPrimary());
    const auto baseline = static_cast<int>(std::round(bounds.getCentreY() + (valueFont.getAscent() - valueFont.getDescent()) * 0.5f));
    g.drawSingleLineText(valueText, valueRight, baseline, juce::Justification::right);
    if (std::isfinite(value) || value == -INFINITY)
    {
        g.setFont(unitFont);
        g.setColour(ShellTheme::textSecondary());
        g.drawSingleLineText(unit, valueRight + 4, baseline);
    }
}
}
