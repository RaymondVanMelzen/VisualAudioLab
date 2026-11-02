#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/** Component-scoped dark palette; its owner must detach it before destroying descendants. */
class ShellTheme final : public juce::LookAndFeel_V4
{
public:
    ShellTheme();
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getSliderPopupFont(juce::Slider&) override;
    juce::Font getTabButtonFont(juce::TabBarButton&, float) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    static juce::Colour background() noexcept { return juce::Colour(0xff1b1d20); }
    static juce::Colour surface() noexcept { return juce::Colour(0xff262a2d); }
    static juce::Colour raised() noexcept { return juce::Colour(0xff30363a); }
    static juce::Colour input() noexcept { return juce::Colour(0xff17191c); }
    static juce::Colour border() noexcept { return juce::Colour(0xff73808a); }
    static juce::Colour requiredBorder() noexcept { return juce::Colour(0xff91aab2); }
    static juce::Colour textPrimary() noexcept { return juce::Colour(0xfff2f1ee); }
    static juce::Colour textSecondary() noexcept { return juce::Colour(0xffb7bdc1); }
    static juce::Colour textMuted() noexcept { return juce::Colour(0xff9aa3a9); }
    static juce::Colour textDisabled() noexcept { return juce::Colour(0xff7b848a); }
    static juce::Colour audio() noexcept { return juce::Colour(0xff00c2d1); }
    static juce::Colour midi() noexcept { return juce::Colour(0xffcc5eff); }
    static juce::Colour control() noexcept { return juce::Colour(0xffe4793a); }
    static juce::Colour analyzer() noexcept { return juce::Colour(0xff9ddc4a); }
    static juce::Colour selected() noexcept { return juce::Colour(0xfff39a57); }
    static juce::Colour warning() noexcept { return juce::Colour(0xffd7a93f); }
    static juce::Colour error() noexcept { return juce::Colour(0xffd45353); }
    static juce::Colour errorText() noexcept { return juce::Colour(0xffff8a8a); }
    static juce::Colour hover() noexcept { return juce::Colour(0xff343b40); }
    static juce::Colour pressed() noexcept { return juce::Colour(0xff3a4247); }
    static juce::Colour focus() noexcept { return textPrimary(); }
    static juce::Colour onAccent() noexcept { return juce::Colour(0xff121416); }
    static bool glowEnabled() noexcept;
    static void setGlowEnabled(bool enabled) noexcept;
};
}
