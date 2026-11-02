#include <val/presentation/ConnectionView.h>
#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
ConnectionView::ConnectionView(juce::Point<float> source, juce::Point<float> destination)
{
    const auto bend = juce::jmax(45.0f, std::abs(destination.x - source.x) * 0.45f);
    path_.startNewSubPath(source);
    path_.cubicTo(source.x + bend, source.y, destination.x - bend, destination.y,
                  destination.x, destination.y);
}

bool ConnectionView::hit(juce::Point<float> point, float tolerance) const
{
    juce::Point<float> nearest;
    path_.getNearestPoint(point, nearest);
    return nearest.getDistanceFrom(point) <= tolerance;
}

void ConnectionView::paint(juce::Graphics& graphics, bool selected) const
{
    if (selected)
    {
        graphics.setColour(ShellTheme::selected());
        graphics.strokePath(path_, juce::PathStrokeType(4.5f));
    }
    graphics.setColour(ShellTheme::audio());
    graphics.strokePath(path_, juce::PathStrokeType(selected ? 2.0f : 2.5f));
}
}
