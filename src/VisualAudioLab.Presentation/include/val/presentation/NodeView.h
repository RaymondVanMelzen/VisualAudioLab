#pragma once

#include <val/domain/Graph.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
/**
 * Lightweight node renderer and patch-space geometry, not one native component per node.
 * CanvasView supplies current model values and selection; no borrowed node reference survives
 * a paint or hit-test, allowing undo to replace model objects without stale view pointers.
 */
class NodeView final
{
public:
    static constexpr float width = 168.0f, height = 88.0f;
    [[nodiscard]] static juce::Rectangle<float> bounds(domain::Position position);
    [[nodiscard]] static juce::Point<float> port(domain::Position position, domain::Port direction);
    static void paint(juce::Graphics& graphics, const domain::Node& node,
                      domain::Position position, bool selected, bool focused, bool detail = true);
};
}
