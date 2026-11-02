#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
namespace
{
bool drawGlow = true;
}

ShellTheme::ShellTheme()
{
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
