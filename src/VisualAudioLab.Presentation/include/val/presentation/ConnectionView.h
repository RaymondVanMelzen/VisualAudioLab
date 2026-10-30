#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/** Cached patch-space cable path with zoom-independent screen-space hit tolerance. */
class ConnectionView final
{
public:
    ConnectionView() = default;
    ConnectionView(juce::Point<float> source, juce::Point<float> destination);
    [[nodiscard]] bool hit(juce::Point<float> patchPoint, float patchTolerance) const;
    void paint(juce::Graphics& graphics, bool selected) const;
    [[nodiscard]] const juce::Path& path() const noexcept { return path_; }
    [[nodiscard]] juce::Rectangle<float> bounds() const { return path_.getBounds(); }

private:
    juce::Path path_;
};
}
