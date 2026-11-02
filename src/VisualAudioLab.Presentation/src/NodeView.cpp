#include <val/presentation/NodeView.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>
#include <val/domain/Routing.h>

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
                     domain::Position position, bool selected, bool focused, float zoom)
{
    const auto rectangle = bounds(position);
    const auto detail = Typography::detail(zoom);
    if (detail == TextDetail::glyph)
    {
        graphics.setColour(ShellTheme::surface());
        graphics.fillRect(rectangle);
        graphics.setColour(ShellTheme::border());
        graphics.drawRect(rectangle, 1.0f);
        if (selected)
        {
            graphics.setColour(ShellTheme::selected());
            graphics.fillRect(rectangle.withWidth(3.0f));
        }
        graphics.setFont(Typography::ui(Typography::canvasHeight(12, zoom, 12), true));
        graphics.setColour(ShellTheme::textPrimary());
        graphics.drawText(juce::String::fromUTF8(node.name.c_str()),
                          rectangle.withSizeKeepingCentre(std::max(width, 120.0f / zoom), 18.0f / zoom),
                          juce::Justification::centred, true);
        return;
    }
    if (selected && ShellTheme::glowEnabled())
    {
        graphics.setColour(ShellTheme::selected().withAlpha(0.12f));
        graphics.drawRoundedRectangle(rectangle.expanded(9.0f), 15.0f, 3.0f);
        graphics.setColour(ShellTheme::selected().withAlpha(0.16f));
        graphics.drawRoundedRectangle(rectangle.expanded(6.0f), 12.0f, 2.0f);
        graphics.setColour(ShellTheme::selected().withAlpha(0.20f));
        graphics.drawRoundedRectangle(rectangle.expanded(3.0f), 9.0f, 1.0f);
    }
    graphics.setColour(ShellTheme::surface());
    graphics.fillRoundedRectangle(rectangle, 6.0f);
    graphics.setColour(ShellTheme::border());
    graphics.drawRoundedRectangle(rectangle, 6.0f, 1.0f);
    graphics.setColour(ShellTheme::raised());
    graphics.fillRect(rectangle.withHeight(30.0f).reduced(3.0f));
    const auto field = node.parameters.find("audio.type");
    const auto* type = field == node.parameters.end() ? nullptr : std::get_if<std::string>(&field->second);
    if (type && *type == "Analyzer")
    {
        graphics.setColour(ShellTheme::analyzer());
        graphics.fillRect(rectangle.withX(rectangle.getRight() - 3.0f).withWidth(2.0f).reduced(0.0f, 6.0f));
    }
    graphics.setColour(ShellTheme::textPrimary());
    graphics.setFont(Typography::ui(Typography::canvasHeight(12, zoom, 11), true));
    graphics.drawText(juce::String::fromUTF8(node.name.c_str()) + " #" + juce::String(node.id),
                      rectangle.reduced(10.0f, 4.0f).withHeight(std::max(22.0f, 16.0f / zoom)),
                      juce::Justification::centredLeft);
    graphics.setColour(ShellTheme::textSecondary());
    const auto role = !type ? "Routing draft (no DSP)" : (*type == "Input" || *type == "Output")
        ? "Device bus" : type && *type == "Sum" ? "Unity sum (no limiter)" : *type == "Analyzer" ? "Analysis" : "Module";
    if (detail == TextDetail::full || detail == TextDetail::compact)
    {
        graphics.setFont(Typography::ui(Typography::canvasHeight(10.5f, zoom, 10)));
        graphics.drawText(role, rectangle.withTrimmedTop(31).reduced(10, 0).withHeight(std::max(14.0f, 12.0f / zoom)),
                          juce::Justification::centredLeft);
    }
    graphics.setFont(Typography::ui(Typography::canvasHeight(10.5f, zoom, 10)));
    if (detail != TextDetail::overview && domain::hasInputPort(node))
        graphics.drawText(domain::isSumPort(node) ? "Sum In" : "In", rectangle.withTrimmedTop(43).reduced(13, 0).withWidth(65),
                      juce::Justification::centredLeft);
    if (detail != TextDetail::overview && domain::hasOutputPort(node))
        graphics.drawText("Out", rectangle.withTrimmedTop(43).reduced(13, 0),
                      juce::Justification::centredRight);
    for (auto direction : {domain::Port::input, domain::Port::output})
    {
        if ((direction == domain::Port::input && !domain::hasInputPort(node))
            || (direction == domain::Port::output && !domain::hasOutputPort(node))) continue;
        const auto point = port(position, direction);
        graphics.setColour(ShellTheme::audio());
        graphics.fillEllipse(point.x - 5, point.y - 5, 10, 10);
        graphics.setColour(ShellTheme::background());
        graphics.drawEllipse(point.x - 5, point.y - 5, 10, 10, direction == domain::Port::input ? 2.0f : 1.0f);
    }
    if (selected)
    {
        graphics.setColour(ShellTheme::selected());
        graphics.drawRoundedRectangle(rectangle, 6.0f, 2.5f);
        for (const auto point : {rectangle.getTopLeft(), rectangle.getTopRight(),
                                 rectangle.getBottomLeft(), rectangle.getBottomRight()})
            graphics.fillRect(juce::Rectangle<float>(point.x - 3.0f, point.y - 3.0f, 6.0f, 6.0f));
    }
    if (focused)
    {
        graphics.setColour(ShellTheme::focus());
        graphics.drawRoundedRectangle(rectangle.expanded(4.0f), 10.0f, 1.5f);
    }
}
}
