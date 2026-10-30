#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/** Component-scoped dark palette; its owner must detach it before destroying descendants. */
class ShellTheme final : public juce::LookAndFeel_V4
{
public:
    ShellTheme();
    static juce::Colour background() noexcept { return juce::Colour(0xff171b22); }
    static juce::Colour surface() noexcept { return juce::Colour(0xff222833); }
    static juce::Colour border() noexcept { return juce::Colour(0xff465265); }
    static juce::Colour accent() noexcept { return juce::Colour(0xff89c4ff); }
};
}
