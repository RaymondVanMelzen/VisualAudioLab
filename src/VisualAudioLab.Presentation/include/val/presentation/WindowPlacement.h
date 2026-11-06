#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
struct WindowPlacement final
{
    juce::Rectangle<int> bounds;
    int minimumWidth{}, minimumHeight{}, maximumWidth{}, maximumHeight{};
};

/** Fits logical window bounds and resize constraints inside a monitor's usable logical area. */
[[nodiscard]] inline WindowPlacement fitWindowToUserArea(juce::Rectangle<int> desired,
                                                         juce::Rectangle<int> userArea)
{
    if (userArea.isEmpty())
        return {desired, 1, 1, 7680, 4320};

    const auto maximumWidth = juce::jmin(7680, userArea.getWidth());
    const auto maximumHeight = juce::jmin(4320, userArea.getHeight());
    const auto minimumWidth = juce::jmin(640, maximumWidth);
    const auto minimumHeight = juce::jmin(380, maximumHeight);
    const auto width = juce::jlimit(minimumWidth, maximumWidth,
                                    desired.getWidth() > 0 ? desired.getWidth() : 1100);
    const auto height = juce::jlimit(minimumHeight, maximumHeight,
                                     desired.getHeight() > 0 ? desired.getHeight() : 720);
    const auto x = juce::jlimit(userArea.getX(), userArea.getRight() - width, desired.getX());
    const auto y = juce::jlimit(userArea.getY(), userArea.getBottom() - height, desired.getY());
    return {{x, y, width, height}, minimumWidth, minimumHeight, maximumWidth, maximumHeight};
}

/** A restored rectangle on a removed display is recovered on the primary display. */
[[nodiscard]] inline const juce::Displays::Display* displayForRestoredBounds(
    const juce::Displays& displays, juce::Rectangle<int> desired) noexcept
{
    for (const auto& display : displays.displays)
        if (display.totalArea.intersects(desired))
            return displays.getDisplayForRect(desired);
    return displays.getPrimaryDisplay();
}
}
