#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
ShellTheme::ShellTheme()
{
    setColourScheme(getDarkColourScheme());
    setColour(juce::ResizableWindow::backgroundColourId, background());
    setColour(juce::Label::textColourId, juce::Colour(0xffe0e7f0));
    setColour(juce::TextButton::buttonColourId, surface());
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e7f0));
    setColour(juce::ComboBox::backgroundColourId, surface());
    setColour(juce::ComboBox::outlineColourId, border());
    setColour(juce::TextEditor::backgroundColourId, background());
    setColour(juce::TextEditor::textColourId, juce::Colour(0xffe0e7f0));
    setColour(juce::TextEditor::outlineColourId, border());
    setColour(juce::TextEditor::focusedOutlineColourId, accent());
    setColour(juce::PopupMenu::backgroundColourId, surface());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff344960));
    setColour(juce::TabbedComponent::backgroundColourId, background());
    setColour(juce::TabbedComponent::outlineColourId, border());
}
}
