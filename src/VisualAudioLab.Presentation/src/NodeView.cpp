#include <val/presentation/NodeView.h>
#include <val/presentation/ShellTheme.h>

namespace val::presentation
{
juce::Rectangle<float> NodeView::bounds(domain::Position position)
{
    return {static_cast<float>(position.x), static_cast<float>(position.y), width, height};
}

juce::Point<float> NodeView::port(domain::Position position, domain::Port direction)
{
    const auto rectangle = bounds(position);
    return {direction == domain::Port::input ? rectangle.getX() : rectangle.getRight(),
            rectangle.getY() + 58.0f};
}

void NodeView::paint(juce::Graphics& graphics, const domain::Node& node,
                     domain::Position position, bool selected, bool focused, bool detail)
{
    const auto rectangle = bounds(position);
    if (!detail)
    {
        graphics.setColour(selected ? ShellTheme::accent() : ShellTheme::border());
        graphics.fillRect(rectangle);
        return;
    }
    graphics.setColour(ShellTheme::surface());
    graphics.fillRoundedRectangle(rectangle, 6.0f);
    graphics.setColour(selected ? ShellTheme::accent() : ShellTheme::border());
    graphics.drawRoundedRectangle(rectangle, 6.0f, selected ? 2.5f : 1.0f);
    graphics.setColour(juce::Colour(0xff303c4e));
    graphics.fillRect(rectangle.withHeight(30.0f).reduced(3.0f));
    graphics.setColour(juce::Colours::white);
    graphics.drawText(juce::String::fromUTF8(node.name.c_str()) + " #" + juce::String(node.id),
                      rectangle.reduced(10.0f, 4.0f).withHeight(22.0f),
                      juce::Justification::centredLeft);
    graphics.setColour(juce::Colours::lightgrey);
    graphics.drawText("In", rectangle.withTrimmedTop(43).reduced(13, 0).withWidth(45),
                      juce::Justification::centredLeft);
    graphics.drawText("Out", rectangle.withTrimmedTop(43).reduced(13, 0),
                      juce::Justification::centredRight);
    for (auto direction : {domain::Port::input, domain::Port::output})
    {
        const auto point = port(position, direction);
        graphics.setColour(direction == domain::Port::input ? juce::Colours::lightgrey : ShellTheme::accent());
        graphics.fillEllipse(point.x - 5, point.y - 5, 10, 10);
        graphics.setColour(ShellTheme::background());
        graphics.drawEllipse(point.x - 5, point.y - 5, 10, 10, 1.0f);
    }
    if (focused)
    {
        graphics.setColour(juce::Colours::white);
        graphics.drawRoundedRectangle(rectangle.expanded(4), 7.0f, 1.0f);
    }
}
}
