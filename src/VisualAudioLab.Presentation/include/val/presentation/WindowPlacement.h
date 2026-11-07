#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
struct WindowPlacement final
{
    juce::Rectangle<int> bounds;
    int minimumWidth{}, minimumHeight{}, maximumWidth{}, maximumHeight{};
};

/** Fits logical client bounds and outer-window resize constraints inside a usable display area. */
[[nodiscard]] inline WindowPlacement fitWindowToUserArea(juce::Rectangle<int> desired,
                                                         juce::Rectangle<int> userArea,
                                                         juce::BorderSize<int> frame = {})
{
    if (userArea.isEmpty())
        return {desired, 1, 1, 7680, 4320};

    const auto maximumWidth = juce::jmin(7680, userArea.getWidth());
    const auto maximumHeight = juce::jmin(4320, userArea.getHeight());
    const auto minimumWidth = juce::jmin(640, maximumWidth);
    const auto minimumHeight = juce::jmin(380, maximumHeight);
    const auto frameWidth = frame.getLeftAndRight();
    const auto frameHeight = frame.getTopAndBottom();
    const auto outerWidth = juce::jlimit(minimumWidth, maximumWidth,
        (desired.getWidth() > 0 ? desired.getWidth() : 1100) + frameWidth);
    const auto outerHeight = juce::jlimit(minimumHeight, maximumHeight,
        (desired.getHeight() > 0 ? desired.getHeight() : 720) + frameHeight);
    const auto width = juce::jmax(1, outerWidth - frameWidth);
    const auto height = juce::jmax(1, outerHeight - frameHeight);
    const auto outerX = juce::jlimit(userArea.getX(), userArea.getRight() - outerWidth,
                                     desired.getX() - frame.getLeft());
    const auto outerY = juce::jlimit(userArea.getY(), userArea.getBottom() - outerHeight,
                                     desired.getY() - frame.getTop());
    return {{outerX + frame.getLeft(), outerY + frame.getTop(), width, height},
            minimumWidth, minimumHeight, maximumWidth, maximumHeight};
}

/** Returns the native non-client frame in logical units when the peer has reported it. */
[[nodiscard]] inline juce::BorderSize<int> windowFrameSize(const juce::Component& component) noexcept
{
    if (const auto* peer = component.getPeer())
        if (const auto frame = peer->getFrameSizeIfPresent())
            return *frame;
    return {};
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
