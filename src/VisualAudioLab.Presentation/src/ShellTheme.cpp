#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>

namespace val::presentation
{
namespace
{
bool drawGlow = true;
}

ShellTheme::ShellTheme()
{
    setDefaultSansSerifTypeface(Typography::ui(12).getTypefacePtr());
    setColourScheme(getDarkColourScheme());
    setColour(juce::ResizableWindow::backgroundColourId, background());
    setColour(juce::Label::textColourId, textPrimary());
    setColour(juce::TextButton::buttonColourId, surface());
    setColour(juce::TextButton::buttonOnColourId, raised());
    setColour(juce::TextButton::textColourOffId, textPrimary());
    setColour(juce::TextButton::textColourOnId, textPrimary());
    setColour(juce::ComboBox::backgroundColourId, surface());
    setColour(juce::ComboBox::outlineColourId, border());
    setColour(juce::ComboBox::focusedOutlineColourId, focus());
    setColour(juce::TextEditor::backgroundColourId, input());
    setColour(juce::TextEditor::textColourId, textPrimary());
    setColour(juce::TextEditor::outlineColourId, border());
    setColour(juce::TextEditor::focusedOutlineColourId, focus());
    setColour(juce::TextEditor::highlightedTextColourId, textPrimary());
    setColour(juce::PopupMenu::backgroundColourId, surface());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, hover());
    setColour(juce::Slider::thumbColourId, audio());
    setColour(juce::Slider::trackColourId, border());
    setColour(juce::Slider::backgroundColourId, input());
    setColour(juce::TextEditor::highlightColourId, selected().withAlpha(0.35f));
    setColour(juce::TabbedComponent::backgroundColourId, background());
    setColour(juce::TabbedComponent::outlineColourId, border());
}

juce::Font ShellTheme::getTextButtonFont(juce::TextButton&, int) { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getComboBoxFont(juce::ComboBox&) { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getLabelFont(juce::Label& label)
{
    const auto current = label.getFont();
    if (current.getTypefaceName() != juce::Font::getDefaultSansSerifFontName()) return current;
    return Typography::ui(std::min(14.0f, current.getHeight()));
}
juce::Font ShellTheme::getPopupMenuFont() { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getAlertWindowTitleFont() { return Typography::ui(14, true); }
juce::Font ShellTheme::getAlertWindowMessageFont() { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getAlertWindowFont() { return Typography::font(TextRole::menu); }
juce::Font ShellTheme::getSliderPopupFont(juce::Slider&) { return Typography::font(TextRole::value); }
juce::Font ShellTheme::getTabButtonFont(juce::TabBarButton&, float) { return Typography::font(TextRole::panelTitle); }
juce::Label* ShellTheme::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(Typography::font(TextRole::value));
    label->setMinimumHorizontalScale(1.0f);
    return label;
}
void ShellTheme::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto tick = 16.0f;
    drawTickBox(g, button, 4, (button.getHeight() - tick) * 0.5f, tick, tick,
                button.getToggleState(), button.isEnabled(), highlighted, down);
    g.setFont(Typography::font(TextRole::menu));
    g.setColour(button.isEnabled() ? textPrimary() : textDisabled());
    g.drawText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(26),
               juce::Justification::centredLeft);
    if (button.hasKeyboardFocus(true))
    {
        g.setColour(focus());
        g.drawRect(button.getLocalBounds().reduced(1), 1);
    }
}

void ShellTheme::drawButtonBackground(juce::Graphics& graphics, juce::Button& button,
                                      const juce::Colour& backgroundColour,
                                      bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = backgroundColour;
    if (!button.isEnabled())
        fill = surface();
    else if (down)
        fill = pressed();
    else if (highlighted)
        fill = hover();
    else if (button.getToggleState())
        fill = raised();

    graphics.setColour(fill);
    graphics.fillRoundedRectangle(bounds, 4.0f);
    graphics.setColour(button.hasKeyboardFocus(true) ? focus()
                       : button.getToggleState() ? selected() : border());
    graphics.drawRoundedRectangle(bounds, 4.0f, button.hasKeyboardFocus(true) ? 1.5f : 1.0f);
    if (button.hasKeyboardFocus(true))
    {
        graphics.setColour(focus());
        graphics.drawRoundedRectangle(bounds.expanded(2.0f), 5.0f, 1.0f);
    }
}

bool ShellTheme::glowEnabled() noexcept
{
    return drawGlow;
}

void ShellTheme::setGlowEnabled(bool enabled) noexcept
{
    drawGlow = enabled;
}
}
