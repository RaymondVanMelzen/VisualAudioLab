#include <val/presentation/CanvasView.h>
#include <val/presentation/ModuleCatalogItems.h>
#include <val/presentation/ShellTheme.h>
#include <val/presentation/Typography.h>
#include <val/domain/Routing.h>
#include <cmath>
#include <vector>

namespace val::presentation
{
CanvasView::CanvasView(application::WorkspaceService& workspace) : workspace_(workspace)
{
    setName("Patch Canvas structural node editor");
    setOpaque(true);
    setWantsKeyboardFocus(true);
    add_.onClick = [this] { createNodeAt(viewport().getCentre().toFloat()); };
    remove_.onClick = [this] { deleteSelection(); };
    fit_.onClick = [this] { fitGraph(); };
    minus_.onClick = [this] { zoomAt(zoom_ / 1.2f, viewport().getCentre().toFloat()); };
    plus_.onClick = [this] { zoomAt(zoom_ * 1.2f, viewport().getCentre().toFloat()); };
    for (auto* child : std::initializer_list<juce::Component*>{
             &add_, &addInput_, &addOutput_, &remove_, &fit_, &minus_, &plus_, &zoomLabel_, &help_})
        addAndMakeVisible(child);
    for (auto* button : {&add_, &addInput_, &addOutput_, &remove_, &fit_, &minus_, &plus_})
        button->setWantsKeyboardFocus(false);
    addInput_.setTooltip("Add an unassigned hardware input row. Nothing is selected or connected.");
    addOutput_.setTooltip("Add an unassigned hardware output row. Nothing is selected or connected.");
    addInput_.onClick = [this]
    {
        std::size_t count{};
        for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        {
            juce::ignoreUnused(id);
            if (endpoint.role == domain::EndpointRole::hardwareInput) ++count;
        }
        finish(workspace_.addHardwareEndpoint(domain::EndpointRole::hardwareInput,
                                               "Hardware Input " + std::to_string(count + 1)));
    };
    addOutput_.onClick = [this]
    {
        std::size_t count{};
        for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        {
            juce::ignoreUnused(id);
            if (endpoint.role == domain::EndpointRole::hardwareOutput) ++count;
        }
        finish(workspace_.addHardwareEndpoint(domain::EndpointRole::hardwareOutput,
                                               "Output " + std::to_string(count + 1)));
    };
    help_.setText("N: add | Drag ports: wire | Wheel: zoom | Middle/Space-drag: pan",
                   juce::dontSendNotification);
    help_.setFont(Typography::font(TextRole::status));
    help_.setTooltip("Shift adds selection; Ctrl/Command toggles. Empty drag selects a box. "
                     "Delete removes selection. M then arrows/Enter moves; Escape cancels. "
                     "Arrows navigate nodes; Tab leaves canvas. C then arrows/Enter connects; D disconnects input. "
                     "Right-click a wire to disconnect. Ctrl/Command+Z undoes.");
    synchronise();
}

CanvasView::~CanvasView() = default;

bool CanvasView::isInterestedInDragSource(const SourceDetails& details)
{
    return isModuleCatalogType(details.description.toString().toStdString());
}

void CanvasView::itemDropped(const SourceDetails& details)
{
    if (!isInterestedInDragSource(details))
        return;
    if (!viewport().contains(details.localPosition))
    {
        if (onModuleDropRejected)
            onModuleDropRejected("Drop the module inside the Patch Canvas area; nothing was inserted.");
        return;
    }
    if (onModuleDropped)
        onModuleDropped(details.description.toString().toStdString(), details.localPosition.toFloat());
}

void CanvasView::restoreNavigation(application::PatchViewport value)
{
    cancelGesture();
    zoom_ = static_cast<float>(value.zoom);
    offset_ = {static_cast<float>(value.x), static_cast<float>(value.y)};
    updateDetails();
    repaint();
}

void CanvasView::resetForProject()
{
    cancelGesture();
    selected_.clear();
    selectedWire_ = focusedNode_ = 0;
    synchronise();
    restoreNavigation(workspace_.project().patches.at(workspace_.project().activePatch).viewport);
}

juce::Rectangle<int> CanvasView::viewport() const
{
    auto area = contentBounds();
    return area.withTrimmedLeft(static_cast<int>(railWidth())).withTrimmedRight(static_cast<int>(railWidth()));
}

juce::Rectangle<int> CanvasView::contentBounds() const
{
    return getLocalBounds().withTrimmedTop(34).withTrimmedBottom(26);
}

float CanvasView::railWidth() const noexcept
{
    if (getWidth() < 390)
        return std::max(72.0f, (getWidth() - 170.0f) * 0.5f);
    return std::min(150.0f, std::max(110.0f, getWidth() * 0.21f));
}

juce::Rectangle<int> CanvasView::sourceRailBounds() const
{
    return contentBounds().withWidth(static_cast<int>(railWidth()));
}

juce::Rectangle<int> CanvasView::destinationRailBounds() const
{
    const auto area = contentBounds();
    return area.withX(area.getRight() - static_cast<int>(railWidth())).withWidth(static_cast<int>(railWidth()));
}

std::vector<domain::GraphId> CanvasView::sourceEndpointIds() const
{
    std::vector<domain::GraphId> result;
    for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        if (endpoint.role == domain::EndpointRole::hardwareInput)
            result.push_back(id);
    for (const auto& [id, node] : workspace_.graph().nodes)
        if (domain::isFixedEndpoint(node) && domain::audioType(node) != "Output")
            result.push_back(id);
    return result;
}

std::vector<domain::GraphId> CanvasView::destinationEndpointIds() const
{
    std::vector<domain::GraphId> result;
    for (const auto& [id, endpoint] : workspace_.graph().endpoints)
        if (endpoint.role == domain::EndpointRole::hardwareOutput)
            result.push_back(id);
    for (const auto& [id, node] : workspace_.graph().nodes)
        if (domain::audioType(node) == "Output")
            result.push_back(id);
    return result;
}

bool CanvasView::fixedEndpoint(domain::GraphId id) const
{
    if (workspace_.graph().endpoints.contains(id))
        return true;
    const auto found = workspace_.graph().nodes.find(id);
    return found != workspace_.graph().nodes.end() && domain::isFixedEndpoint(found->second);
}

juce::Rectangle<int> CanvasView::endpointCardBounds(domain::GraphId id) const
{
    const auto sources = sourceEndpointIds();
    const auto destinations = destinationEndpointIds();
    const auto source = std::find(sources.begin(), sources.end(), id);
    const auto destination = std::find(destinations.begin(), destinations.end(), id);
    const bool isSource = source != sources.end();
    const bool isDestination = destination != destinations.end();
    if (!isSource && !isDestination)
        return {};
    const auto rail = isSource ? sourceRailBounds() : destinationRailBounds();
    const auto index = static_cast<int>(isSource ? std::distance(sources.begin(), source)
                                                  : std::distance(destinations.begin(), destination));
    const auto scroll = isSource ? sourceScroll_ : destinationScroll_;
    return {rail.getX() + 6, rail.getY() + 86 + index * 70 - scroll,
            std::max(50, rail.getWidth() - 12), 62};
}

std::optional<juce::Point<float>> CanvasView::endpointPortPosition(domain::GraphId id, domain::Port direction) const
{
    if (const auto endpoint = workspace_.graph().endpoints.find(id); endpoint != workspace_.graph().endpoints.end())
    {
        const bool source = endpoint->second.role == domain::EndpointRole::hardwareInput;
        if ((source && direction != domain::Port::output) || (!source && direction != domain::Port::input))
            return std::nullopt;
        const auto card = endpointCardBounds(id);
        return juce::Point<float>{source ? static_cast<float>(card.getRight() - 1)
                                         : static_cast<float>(card.getX() + 1),
                                  static_cast<float>(card.getCentreY())};
    }
    const auto found = workspace_.graph().nodes.find(id);
    if (found == workspace_.graph().nodes.end())
        return std::nullopt;
    const bool source = domain::audioType(found->second) != "Output" && domain::isFixedEndpoint(found->second);
    const bool destination = domain::audioType(found->second) == "Output";
    if ((source && direction != domain::Port::output) || (destination && direction != domain::Port::input)
        || (!source && !destination))
        return std::nullopt;
    const auto card = endpointCardBounds(id);
    return juce::Point<float>{source ? static_cast<float>(card.getRight() - 1)
                                     : static_cast<float>(card.getX() + 1),
                              static_cast<float>(card.getCentreY())};
}

void CanvasView::clampRailScroll()
{
    const auto sourceMaximum = std::max(0, static_cast<int>(sourceEndpointIds().size()) * 70
                                           - std::max(0, contentBounds().getHeight() - 78));
    const auto destinationMaximum = std::max(0, static_cast<int>(destinationEndpointIds().size()) * 70
                                                - std::max(0, contentBounds().getHeight() - 78));
    sourceScroll_ = juce::jlimit(0, sourceMaximum, sourceScroll_);
    destinationScroll_ = juce::jlimit(0, destinationMaximum, destinationScroll_);
}

void CanvasView::resized()
{
    clampRailScroll();
    auto row = getLocalBounds().removeFromTop(32).reduced(2);
    add_.setBounds(row.removeFromLeft(110));
    remove_.setBounds(row.removeFromLeft(65));
    fit_.setBounds(row.removeFromLeft(42));
    minus_.setBounds(row.removeFromLeft(28));
    zoomLabel_.setBounds(row.removeFromLeft(55));
    plus_.setBounds(row.removeFromLeft(28));
    help_.setBounds(getLocalBounds().removeFromBottom(24));
    const auto source = sourceRailBounds();
    const auto destination = destinationRailBounds();
    addInput_.setBounds(source.withTrimmedTop(54).withHeight(24).reduced(6, 0));
    addOutput_.setBounds(destination.withTrimmedTop(54).withHeight(24).reduced(6, 0));
}

juce::Point<float> CanvasView::toPatch(juce::Point<float> screen) const
{
    return (screen - viewport().getPosition().toFloat() - offset_) / zoom_;
}

juce::Point<float> CanvasView::toScreen(juce::Point<float> patch) const
{
    return patch * zoom_ + offset_ + viewport().getPosition().toFloat();
}

juce::Rectangle<float> CanvasView::visiblePatch() const
{
    return {toPatch(viewport().getTopLeft().toFloat()), toPatch(viewport().getBottomRight().toFloat())};
}

domain::Position CanvasView::position(domain::GraphId node) const
{
    if (const auto found = preview_.find(node); found != preview_.end())
        return found->second;
    if (const auto output = endpointPortPosition(node, domain::Port::output))
    {
        const auto patch = toPatch(*output);
        return {patch.x - NodeView::width, patch.y - NodeView::portY};
    }
    if (const auto input = endpointPortPosition(node, domain::Port::input))
    {
        const auto patch = toPatch(*input);
        return {patch.x, patch.y - NodeView::portY};
    }
    return workspace_.graph().nodes.at(node).position;
}

ConnectionView CanvasView::wireView(const domain::Connection& wire) const
{
    return {NodeView::port(position(wire.source.node), wire.source.port),
            NodeView::port(position(wire.destination.node), wire.destination.port)};
}

void CanvasView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ShellTheme::background());
    {
    juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(contentBounds());
    const auto visible = visiblePatch();
    const auto clip = graphics.getClipBounds().getIntersection(contentBounds()).toFloat();
    const juce::Rectangle<float> damaged{toPatch(clip.getTopLeft()), toPatch(clip.getBottomRight())};
    const auto origin = toScreen({0, 0});
    graphics.addTransform(juce::AffineTransform::scale(zoom_).translated(origin.x, origin.y));
    float grid = 32.0f;
    while (grid * zoom_ < 20.0f)
        grid *= 4.0f;
    {
        juce::Graphics::ScopedSaveState patchArea(graphics);
        graphics.reduceClipRegion(viewport());
        graphics.setColour(ShellTheme::border().withAlpha(0.18f));
        for (float x = std::floor(damaged.getX() / grid) * grid; x < damaged.getRight(); x += grid)
            for (float y = std::floor(damaged.getY() / grid) * grid; y < damaged.getBottom(); y += grid)
                graphics.fillRect(x, y, 1.0f / zoom_, 1.0f / zoom_);
    }

    for (auto id : scene_->connections(damaged.expanded(8.0f / zoom_)))
        if (!movingWires_.contains(id)
            && !fixedEndpoint(workspace_.graph().connections.at(id).source.node)
            && !fixedEndpoint(workspace_.graph().connections.at(id).destination.node))
            scene_->wires().at(id).paint(graphics, id == selectedWire_);
    for (const auto& [id, wire] : workspace_.graph().connections)
        if ((fixedEndpoint(wire.source.node) || fixedEndpoint(wire.destination.node))
            && !movingWires_.contains(id))
        {
            const auto view = wireView(wire);
            if (view.bounds().expanded(8.0f / zoom_).intersects(damaged))
                view.paint(graphics, id == selectedWire_);
        }
    for (auto id : movingWires_)
        wireView(workspace_.graph().connections.at(id)).paint(graphics, id == selectedWire_);

    auto candidates = scene_->nodes(damaged.expanded(12.0f / zoom_));
    for (const auto& [id, point] : preview_)
        if (NodeView::bounds(point).intersects(visible))
            candidates.insert(id);
    for (auto id : candidates)
    {
        if (fixedEndpoint(id))
            continue;
        const auto point = position(id);
        if (NodeView::bounds(point).expanded(12.0f / zoom_).intersects(visible))
        {
            NodeView::paint(graphics, workspace_.graph().nodes.at(id), point,
                            selected_.contains(id), focusedNode_ == id && hasKeyboardFocus(true), zoom_);
            if (paintObservation && zoom_ >= 0.65f)
                paintObservation(graphics, id, NodeView::bounds(point).reduced(58, 0).withTrimmedTop(36).withHeight(36));
        }
    }
    if (gesture_ == Gesture::connect && source_)
    {
        const auto start = NodeView::port(position(source_->node), source_->port);
        const auto end = toPatch(pointer_);
        ConnectionView preview(source_->port == domain::Port::output ? start : end,
                                source_->port == domain::Port::output ? end : start);
        preview.paint(graphics, true);
    }
    if (gesture_ == Gesture::marquee)
    {
        graphics.setColour(ShellTheme::selected().withAlpha(0.12f));
        graphics.fillRect(marquee_);
        graphics.setColour(ShellTheme::selected());
        graphics.drawRect(marquee_, 1.0f / zoom_);
    }
    if (workspace_.graph().nodes.empty())
    {
        graphics.setFont(Typography::ui(Typography::canvasHeight(15, zoom_, 14)));
        graphics.setColour(ShellTheme::textSecondary());
        graphics.drawFittedText("Choose a hardware input or built-in generator from Sources.\n"
                                "Assign a named output channel in the Destinations lane.\n"
                                "Processors stay in the centre; connect the ports to build a route.",
                                visible.toNearestInt().reduced(8), juce::Justification::centred, 4);
    }
    }
    paintEndpointRails(graphics);
}

void CanvasView::paintEndpointRails(juce::Graphics& graphics)
{
    const auto sources = sourceEndpointIds();
    const auto destinations = destinationEndpointIds();
    const auto sourceRail = sourceRailBounds();
    const auto destinationRail = destinationRailBounds();
    const auto paintRail = [&](juce::Rectangle<int> rail, bool source)
    {
        juce::Graphics::ScopedSaveState saved(graphics);
        graphics.reduceClipRegion(rail);
        graphics.setColour(ShellTheme::surface().darker(0.35f));
        graphics.fillRect(rail);
        graphics.setColour(ShellTheme::border());
        graphics.drawVerticalLine(source ? rail.getRight() - 1 : rail.getX(), static_cast<float>(rail.getY()),
                                  static_cast<float>(rail.getBottom()));
        graphics.setColour(ShellTheme::textPrimary());
        graphics.setFont(Typography::font(TextRole::panelTitle));
        graphics.drawText(source ? "Sources" : "Destinations",
                          rail.withTrimmedTop(8).withHeight(23).reduced(8, 0), juce::Justification::centredLeft);
        graphics.setColour(ShellTheme::textSecondary());
        graphics.setFont(Typography::font(TextRole::status));
        graphics.drawFittedText(source ? "Hardware input and built-in generators"
                                       : "Hardware output rows",
                               rail.withTrimmedTop(32).withHeight(20).reduced(8, 0),
                               juce::Justification::centredLeft, 1);

        const auto& endpoints = source ? sources : destinations;
        if (endpoints.empty())
        {
            auto placeholder = rail.withTrimmedTop(86).reduced(6, 0).withHeight(76);
            graphics.setColour(ShellTheme::raised());
            graphics.fillRoundedRectangle(placeholder.toFloat(), 5);
            graphics.setColour(ShellTheme::textPrimary());
            graphics.setFont(Typography::ui(15, true));
            graphics.drawText(source ? "No source rows" : "No output rows",
                              placeholder.reduced(7, 5).withHeight(22), juce::Justification::centredLeft);
            graphics.setColour(ShellTheme::textSecondary());
            graphics.setFont(Typography::ui(14));
            graphics.drawFittedText(source ? "Add an input row or generator."
                                          : "Add an output row and assign channels.",
                                   placeholder.reduced(7, 27), juce::Justification::topLeft, 3);
            return;
        }
        for (auto id : endpoints)
        {
            const auto card = endpointCardBounds(id);
            if (!rail.intersects(card))
                continue;
            const auto hardware = workspace_.graph().endpoints.find(id);
            const auto node = workspace_.graph().nodes.find(id);
            const bool isHardware = hardware != workspace_.graph().endpoints.end();
            const auto type = node != workspace_.graph().nodes.end() ? domain::audioType(node->second) : std::string_view{};
            const auto title = isHardware ? juce::String::fromUTF8(hardware->second.name.c_str())
                : juce::String::fromUTF8(node->second.name.c_str());
            graphics.setColour(ShellTheme::raised());
            graphics.fillRoundedRectangle(card.toFloat(), 5);
            graphics.setColour(ShellTheme::border());
            graphics.drawRoundedRectangle(card.toFloat(), 5, 1);
            graphics.setColour(ShellTheme::textPrimary());
            graphics.setFont(Typography::ui(14, true));
            graphics.drawFittedText(title, card.reduced(7, 3).withHeight(19),
                                    juce::Justification::centredLeft, 1);
            graphics.setColour(ShellTheme::textSecondary());
            graphics.setFont(Typography::ui(12));
            juce::String channelSummary;
            juce::String readiness;
            if (isHardware)
            {
                juce::StringArray names;
                for (const auto& channel : hardware->second.channelNames)
                    names.add(juce::String::fromUTF8(channel.c_str()));
                channelSummary = names.isEmpty() ? juce::String::fromUTF8("Unassigned · ")
                    + juce::String(hardware->second.layoutChannels) + " ch"
                                                  : names.joinIntoString(" / ");
                readiness = endpointStatus ? endpointStatus(id) : "Device status unavailable";
                if (!hardware->second.enabled) readiness = "Disabled";
            }
            else
            {
                channelSummary = type == "Input" ? "Legacy input endpoint"
                    : type == "Output" ? "Legacy output endpoint" : "Built-in generator";
            }
            graphics.drawFittedText(channelSummary, card.reduced(7, 23).withHeight(17),
                                    juce::Justification::centredLeft, 1);
            graphics.setFont(Typography::ui(11));
            graphics.drawFittedText(readiness, card.reduced(7, 41).withHeight(16),
                                    juce::Justification::centredLeft, 1);
            if (const auto port = endpointPortPosition(id, source ? domain::Port::output : domain::Port::input))
            {
                graphics.setColour(ShellTheme::audio());
                graphics.fillEllipse(port->x - 5, port->y - 5, 10, 10);
                graphics.setColour(ShellTheme::background());
                graphics.drawEllipse(port->x - 5, port->y - 5, 10, 10, 1);
            }
            if (selected_.contains(id))
            {
                graphics.setColour(ShellTheme::selected());
                graphics.drawRoundedRectangle(card.toFloat(), 5, 2);
            }
            if (focusedNode_ == id && hasKeyboardFocus(true))
            {
                graphics.setColour(ShellTheme::focus());
                graphics.drawRoundedRectangle(card.expanded(3).toFloat(), 7, 1.5f);
            }
        }
        const auto required = static_cast<int>(endpoints.size()) * 70;
        const auto available = std::max(0, rail.getHeight() - 68);
        if (required > available)
        {
            graphics.setColour(ShellTheme::textSecondary());
            graphics.setFont(Typography::font(TextRole::status));
            graphics.drawText("Scroll for more",
                rail.withTrimmedTop(rail.getHeight() - 23).reduced(8, 0).withHeight(18),
                juce::Justification::centredLeft);
        }
    };
    paintRail(sourceRail, true);
    paintRail(destinationRail, false);
}

bool CanvasView::observationVisible(domain::GraphId id) const
{
    if (const auto* peer = getPeer(); peer && peer->isMinimised()) return false;
    const auto found = workspace_.graph().nodes.find(id);
    return isShowing() && zoom_ >= 0.65f && found != workspace_.graph().nodes.end()
        && NodeView::bounds(position(id)).intersects(visiblePatch());
}

void CanvasView::repaintObservation(domain::GraphId id)
{
    if (!observationVisible(id)) return;
    const auto rectangle = NodeView::bounds(position(id));
    repaint(juce::Rectangle<float>(toScreen(rectangle.getTopLeft()), toScreen(rectangle.getBottomRight())).getSmallestIntegerContainer().expanded(1));
}

void CanvasView::updateDetails()
{
    if (onObservationTarget)
    {
        const auto wire = workspace_.graph().connections.find(selectedWire_);
        const auto target = wire != workspace_.graph().connections.end() ? wire->second.source.node
            : selected_.size() == 1 ? *selected_.begin() : 0;
        onObservationTarget(target);
    }
    zoomLabel_.setText(juce::String(juce::roundToInt(zoom_ * 100)) + "%", juce::dontSendNotification);
    remove_.setEnabled(!selected_.empty() || selectedWire_ != 0);
    setDescription("Fixed Sources rail on the left; fixed Destinations rail on the right. "
                    "Hardware device and exact named channels are selected in Audio Setup. "
                    "Built-in generators appear as sources. Processors remain movable in the centre patch area. "
                    + juce::String(workspace_.graph().nodes.size()) + " graph nodes, "
                    + juce::String(selected_.size()) + " selected. Focused node "
                    + juce::String(focusedNode_) + ". " + help_.getTooltip());
}

void CanvasView::synchronise()
{
    cancelGesture();
    const auto& graph = workspace_.graph();
    std::erase_if(selected_, [&](auto id) { return !graph.nodes.contains(id) && !graph.endpoints.contains(id); });
    clampRailScroll();
    if (!graph.nodes.contains(focusedNode_) && !graph.endpoints.contains(focusedNode_))
        focusedNode_ = selected_.empty() ? 0 : *selected_.begin();
    if (!graph.connections.contains(selectedWire_))
        selectedWire_ = 0;
    scene_ = std::make_unique<CanvasScene>(graph);
    updateDetails();
    repaint();
}

void CanvasView::finish(const application::Result& result)
{
    synchronise();
    if (onEdit)
        onEdit(result);
}

void CanvasView::createNodeAt(juce::Point<float> point)
{
    cancelGesture();
    const auto patch = toPatch(point);
    const auto result = workspace_.createNode({patch.x, patch.y}, "Routing draft");
    if (result.succeeded)
    {
        selected_ = {workspace_.lastCreatedNode()};
        selectedWire_ = 0;
        focusedNode_ = workspace_.lastCreatedNode();
    }
    finish(result);
    grabKeyboardFocus();
}

void CanvasView::deleteSelection()
{
    cancelGesture();
    if (!selected_.empty())
    {
        if (std::any_of(selected_.begin(), selected_.end(), [&](auto id) { return fixedEndpoint(id); }))
        {
            finish(application::Result::failure("Sources and destinations stay fixed on their rails and cannot be deleted."));
            return;
        }
        finish(workspace_.deleteNodes({selected_.begin(), selected_.end()}));
    }
    else if (selectedWire_ != 0)
        finish(workspace_.disconnect(selectedWire_));
    else
        finish(application::Result::failure("Select nodes or a connection to delete."));
}

void CanvasView::selectAll()
{
    cancelGesture();
    selected_.clear();
    for (const auto& [id, node] : workspace_.graph().nodes)
    {
        if (!domain::isFixedEndpoint(node))
            selected_.insert(id);
    }
    selectedWire_ = 0;
    updateDetails();
    repaint();
}

void CanvasView::zoomAt(float value, juce::Point<float> point)
{
    if (!std::isfinite(value))
    {
        if (onEdit)
            onEdit(application::Result::failure("Zoom must be a finite value."));
        return;
    }
    cancelGesture();
    const auto anchor = toPatch(point);
    zoom_ = juce::jlimit(0.025f, 3.0f, value);
    offset_ = point - viewport().getPosition().toFloat() - anchor * zoom_;
    updateDetails();
    repaint(viewport());
}

void CanvasView::panBy(juce::Point<float> delta)
{
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
    {
        if (onEdit)
            onEdit(application::Result::failure("Pan distance must be finite."));
        return;
    }
    offset_ += delta;
    offset_.x = juce::jlimit(-3000000.0f, 3000000.0f, offset_.x);
    offset_.y = juce::jlimit(-3000000.0f, 3000000.0f, offset_.y);
    repaint(viewport());
}

void CanvasView::fitGraph()
{
    cancelGesture();
    const auto content = scene_->contentBounds().expanded(40);
    if (!content.isEmpty() && !viewport().isEmpty())
    {
        zoom_ = juce::jlimit(0.025f, 3.0f,
            juce::jmin(viewport().getWidth() / content.getWidth(), viewport().getHeight() / content.getHeight()));
        offset_ = viewport().getCentre().toFloat() - viewport().getPosition().toFloat()
                  - content.getCentre() * zoom_;
        updateDetails();
        repaint(viewport());
    }
}

domain::GraphId CanvasView::nodeAt(juce::Point<float> screen) const
{
    for (auto id : sourceEndpointIds())
        if (endpointCardBounds(id).toFloat().contains(screen))
            return id;
    for (auto id : destinationEndpointIds())
        if (endpointCardBounds(id).toFloat().contains(screen))
            return id;
    if (!viewport().toFloat().contains(screen))
        return 0;
    const auto point = toPatch(screen);
    const auto candidates = scene_->nodes({point.x - 1, point.y - 1, 2, 2});
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
        if (!fixedEndpoint(*it) && NodeView::bounds(position(*it)).contains(point))
            return *it;
    return 0;
}

std::optional<domain::Endpoint> CanvasView::portAt(juce::Point<float> screen) const
{
    for (const auto& [ids, direction] : {std::pair{sourceEndpointIds(), domain::Port::output},
                                        std::pair{destinationEndpointIds(), domain::Port::input}})
        for (auto id : ids)
            if (const auto point = endpointPortPosition(id, direction); point && point->getDistanceFrom(screen) <= 9.0f)
                return domain::Endpoint{id, direction};
    if (!viewport().toFloat().contains(screen))
        return std::nullopt;
    const auto point = toPatch(screen);
    const float tolerance = 9.0f / zoom_;
    const auto candidates = scene_->nodes({point.x - tolerance, point.y - tolerance,
                                           tolerance * 2, tolerance * 2});
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
    {
        if (fixedEndpoint(*it))
            continue;
        for (const auto direction : {domain::Port::input, domain::Port::output})
            if ((direction == domain::Port::input ? domain::hasInputPort(workspace_.graph().nodes.at(*it))
                                                  : domain::hasOutputPort(workspace_.graph().nodes.at(*it)))
                && NodeView::port(position(*it), direction).getDistanceFrom(point) <= tolerance)
                return domain::Endpoint{*it, direction};
    }
    return std::nullopt;
}

domain::GraphId CanvasView::connectionAt(juce::Point<float> screen) const
{
    const auto point = toPatch(screen);
    const auto tolerance = 7.0f / zoom_;
    const auto candidates = scene_->connections({point.x - tolerance, point.y - tolerance,
                                                  tolerance * 2, tolerance * 2});
    for (const auto& [id, wire] : workspace_.graph().connections)
    {
        if (fixedEndpoint(wire.source.node) || fixedEndpoint(wire.destination.node))
        {
            if (wireView(wire).hit(point, tolerance))
                return id;
        }
        else if (candidates.contains(id)
                 && scene_->wires().at(id).hit(point, tolerance))
            return id;
    }
    return 0;
}

void CanvasView::selectRectangle(juce::Rectangle<float> rectangle, bool add, bool toggle)
{
    if (!add && !toggle)
        selected_.clear();
    for (auto id : scene_->nodes(rectangle))
        if (!fixedEndpoint(id) && NodeView::bounds(position(id)).intersects(rectangle))
        {
            if (toggle && selected_.contains(id))
                selected_.erase(id);
            else
                selected_.insert(id);
        }
    selectedWire_ = 0;
    updateDetails();
    repaint(viewport());
}

void CanvasView::mouseDown(const juce::MouseEvent& event)
{
    if (!contentBounds().contains(event.position.toInt()))
        return;
    grabKeyboardFocus();
    cancelGesture();
    down_ = pointer_ = event.position;
    if (event.mods.isPopupMenu())
    {
        const auto row = nodeAt(event.position);
        if (workspace_.graph().endpoints.contains(row))
        {
            endpointMenu(row);
            return;
        }
        contextMenu(event.position);
        return;
    }
    if (viewport().contains(event.position.toInt()) && (event.mods.isMiddleButtonDown() || spaceHeld_))
    {
        gesture_ = Gesture::pan;
        originalOffset_ = offset_;
        return;
    }
    if (auto endpoint = portAt(event.position))
    {
        source_ = endpoint;
        gesture_ = Gesture::connect;
        return;
    }
    const auto node = nodeAt(event.position);
    if (node == 0 && !viewport().contains(event.position.toInt()))
        return;
    if (workspace_.graph().endpoints.contains(node))
    {
        endpointMenu(node);
        return;
    }
    const bool toggle = event.mods.isCommandDown();
    if (node != 0)
    {
        selectedWire_ = 0;
        if (toggle && selected_.contains(node))
            selected_.erase(node);
        else
        {
            if (!toggle && !event.mods.isShiftDown() && !selected_.contains(node))
                selected_.clear();
            selected_.insert(node);
        }
        focusedNode_ = node;
        if (selected_.contains(node)
            && std::none_of(selected_.begin(), selected_.end(), [&](auto id) { return fixedEndpoint(id); }))
        {
            gesture_ = Gesture::move;
            for (auto id : selected_)
            {
                originals_.emplace(id, workspace_.graph().nodes.at(id).position);
                for (auto wire : scene_->incident(id))
                    movingWires_.insert(wire);
            }
        }
    }
    else if (const auto wire = connectionAt(event.position); wire != 0)
    {
        selected_.clear();
        selectedWire_ = wire;
        focusedNode_ = 0;
    }
    else
    {
        beforeSelection_ = selected_;
        gesture_ = Gesture::marquee;
        updateMarquee(event.position, event.mods);
    }
    updateDetails();
    repaint(viewport());
}

juce::Rectangle<int> CanvasView::dirtyPreview() const
{
    juce::Rectangle<float> bounds;
    for (const auto& [id, point] : originals_)
    {
        (void) id;
        bounds = bounds.getUnion(NodeView::bounds(point).expanded(12.0f / zoom_));
    }
    for (const auto& [id, point] : preview_)
    {
        (void) id;
        bounds = bounds.getUnion(NodeView::bounds(point).expanded(12.0f / zoom_));
    }
    for (auto id : movingWires_)
        bounds = bounds.getUnion(wireView(workspace_.graph().connections.at(id)).bounds().expanded(8.0f / zoom_));
    return juce::Rectangle<float>(toScreen(bounds.getTopLeft()), toScreen(bounds.getBottomRight()))
        .getSmallestIntegerContainer().getIntersection(viewport());
}

void CanvasView::updateMove(juce::Point<float> delta)
{
    auto dirty = dirtyPreview();
    for (const auto& [id, start] : originals_)
    {
        // All nodes share one bounded delta, preserving their relative positions at world limits.
        delta.x = juce::jlimit(static_cast<float>(-1000000.0 - start.x),
                               static_cast<float>(1000000.0 - start.x), delta.x);
        delta.y = juce::jlimit(static_cast<float>(-1000000.0 - start.y),
                               static_cast<float>(1000000.0 - start.y), delta.y);
    }
    for (const auto& [id, start] : originals_)
        preview_[id] = {start.x + delta.x, start.y + delta.y};
    dirty = dirty.getUnion(dirtyPreview());
    repaint(dirty);
}

void CanvasView::updateMarquee(juce::Point<float> screen, juce::ModifierKeys modifiers)
{
    marquee_ = {toPatch(down_), toPatch(screen)};
    selected_ = beforeSelection_;
    selectRectangle(marquee_, modifiers.isShiftDown(), modifiers.isCommandDown());
}

void CanvasView::mouseDrag(const juce::MouseEvent& event)
{
    pointer_ = event.position;
    switch (gesture_)
    {
        case Gesture::move:
            if (event.getDistanceFromDragStart() >= 3)
                updateMove((pointer_ - down_) / zoom_);
            break;
        case Gesture::marquee: updateMarquee(pointer_, event.mods); break;
        case Gesture::connect: repaint(viewport()); break;
        case Gesture::pan:
            offset_ = originalOffset_;
            panBy(pointer_ - down_);
            break;
        default: break;
    }
}

void CanvasView::mouseUp(const juce::MouseEvent& event)
{
    if (gesture_ == Gesture::move && !preview_.empty())
    {
        auto positions = preview_;
        cancelGesture();
        finish(workspace_.moveNodes(positions));
    }
    else if (gesture_ == Gesture::connect && source_)
    {
        const auto origin = *source_;
        const auto destination = portAt(event.position);
        cancelGesture();
        if (!destination)
            finish(application::Result::failure("Connection cancelled: release on an opposite-direction port."));
        else
            finish(origin.port == domain::Port::output ? workspace_.connect(origin, *destination)
                                                        : workspace_.connect(*destination, origin));
    }
    else
    {
        // A completed marquee/pan keeps navigation state; cancellation restores it.
        gesture_ = Gesture::none;
        cancelGesture();
    }
}

void CanvasView::cancelGesture()
{
    const bool active = gesture_ != Gesture::none;
    if (gesture_ == Gesture::marquee)
        selected_ = beforeSelection_;
    if (gesture_ == Gesture::pan)
        offset_ = originalOffset_;
    gesture_ = Gesture::none;
    originals_.clear();
    preview_.clear();
    movingWires_.clear();
    source_.reset();
    marquee_ = {};
    if (active)
        repaint(viewport());
}

void CanvasView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (sourceRailBounds().contains(event.position.toInt()) || destinationRailBounds().contains(event.position.toInt()))
    {
        const auto delta = -juce::roundToInt(wheel.deltaY * 140.0f);
        if (sourceRailBounds().contains(event.position.toInt()))
            sourceScroll_ += delta;
        else
            destinationScroll_ += delta;
        clampRailScroll();
        repaint(contentBounds());
        return;
    }
    if (!viewport().contains(event.position.toInt()))
        return;
    if (event.mods.isShiftDown())
        panBy({wheel.deltaX * 150.0f, wheel.deltaY * 150.0f});
    else
        zoomAt(zoom_ * std::pow(1.25f, wheel.deltaY * 5), event.position);
}

bool CanvasView::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const int code = key.getKeyCode() >= 'a' && key.getKeyCode() <= 'z'
                         ? key.getKeyCode() - ('a' - 'A') : key.getKeyCode();
    if (key == juce::KeyPress::escapeKey)
    {
        cancelGesture();
        spaceHeld_ = false;
        updateDetails();
        return true;
    }
    if (modifiers.isCommandDown() && (code == 'Z' || code == 'Y'))
    {
        cancelGesture();
        workspace_.setViewport(navigation());
        finish(modifiers.isShiftDown() || code == 'Y' ? workspace_.redo() : workspace_.undo());
        return true;
    }
    if (modifiers.isCommandDown() && code == 'A')
    {
        selectAll();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        spaceHeld_ = true;
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (code == 'N' && !modifiers.isAnyModifierKeyDown())
    {
        createNodeAt(viewport().getCentre().toFloat());
        return true;
    }
    if (code == 'F' && !modifiers.isAnyModifierKeyDown())
    {
        fitGraph();
        return true;
    }
    if (key.getKeyCode() == '+' || key.getKeyCode() == '=' || key.getKeyCode() == '-')
    {
        zoomAt(zoom_ * (key.getKeyCode() == '-' ? 1.0f / 1.2f : 1.2f), viewport().getCentre().toFloat());
        return true;
    }
    if (code == 'M' && !selected_.empty() && !modifiers.isAnyModifierKeyDown())
    {
        if (std::any_of(selected_.begin(), selected_.end(), [&](auto id) { return fixedEndpoint(id); }))
        {
            if (onEdit)
                onEdit(application::Result::failure("Sources and destinations stay fixed on their rails; only processors can move."));
            return true;
        }
        cancelGesture();
        gesture_ = Gesture::keyboardMove;
        for (auto id : selected_)
        {
            originals_[id] = workspace_.graph().nodes.at(id).position;
            for (auto wire : scene_->incident(id))
                movingWires_.insert(wire);
        }
        preview_ = originals_;
        return true;
    }
    if (gesture_ == Gesture::keyboardMove)
    {
        if (key == juce::KeyPress::returnKey)
        {
            auto positions = preview_;
            cancelGesture();
            finish(workspace_.moveNodes(positions));
            return true;
        }
        juce::Point<float> step;
        if (key == juce::KeyPress::leftKey) step.x = -16;
        else if (key == juce::KeyPress::rightKey) step.x = 16;
        else if (key == juce::KeyPress::upKey) step.y = -16;
        else if (key == juce::KeyPress::downKey) step.y = 16;
        else return false;
        const auto& first = *originals_.begin();
        const auto current = preview_.at(first.first);
        updateMove({static_cast<float>(current.x - first.second.x) + step.x,
                    static_cast<float>(current.y - first.second.y) + step.y});
        return true;
    }
    if (gesture_ == Gesture::connect && source_ && key == juce::KeyPress::returnKey && focusedNode_ != 0)
    {
        const auto source = *source_;
        const auto destination = domain::Endpoint{focusedNode_, domain::Port::input};
        cancelGesture();
        finish(workspace_.connect(source, destination));
        return true;
    }
    if (code == 'C' && focusedNode_ != 0 && !modifiers.isAnyModifierKeyDown())
    {
        cancelGesture();
        gesture_ = Gesture::connect;
        source_ = domain::Endpoint{focusedNode_, domain::Port::output};
        pointer_ = toScreen(NodeView::port(position(focusedNode_), domain::Port::output));
        return true;
    }
    if (code == 'D' && focusedNode_ != 0 && !modifiers.isAnyModifierKeyDown())
    {
        for (auto id : scene_->incident(focusedNode_))
            if (workspace_.graph().connections.at(id).destination.node == focusedNode_)
            {
                finish(workspace_.disconnect(id));
                return true;
            }
        finish(application::Result::failure("The focused node has no incoming wire to disconnect."));
        return true;
    }
    const bool forward = key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey;
    const bool backward = key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey;
    if ((forward || backward) && !workspace_.graph().nodes.empty())
    {
        auto next = workspace_.graph().nodes.upper_bound(focusedNode_);
        if (backward)
        {
            next = workspace_.graph().nodes.lower_bound(focusedNode_);
            if (next == workspace_.graph().nodes.begin())
                next = workspace_.graph().nodes.end();
            --next;
        }
        else if (next == workspace_.graph().nodes.end())
            next = workspace_.graph().nodes.begin();
        focusedNode_ = next->first;
        if (gesture_ == Gesture::connect)
            pointer_ = toScreen(NodeView::port(position(focusedNode_), domain::Port::input));
        else
        {
            if (!modifiers.isShiftDown())
                selected_.clear();
            selected_.insert(focusedNode_);
            selectedWire_ = 0;
        }
        const auto point = fixedEndpoint(next->first) ? endpointCardBounds(next->first).getCentre().toFloat()
                                                       : toScreen(NodeView::bounds(next->second.position).getCentre());
        if (!fixedEndpoint(next->first) && !viewport().contains(point.toInt()))
            panBy(viewport().getCentre().toFloat() - point);
        updateDetails();
        repaint(contentBounds());
        return true;
    }
    return false;
}

bool CanvasView::keyStateChanged(bool)
{
    spaceHeld_ = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey);
    return false;
}

void CanvasView::modifierKeysChanged(const juce::ModifierKeys&)
{
    spaceHeld_ = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey);
}

void CanvasView::focusLost(FocusChangeType)
{
    spaceHeld_ = false;
    cancelGesture();
    updateDetails();
}

void CanvasView::contextMenu(juce::Point<float> screen)
{
    const auto node = nodeAt(screen);
    const auto endpoint = portAt(screen);
    const auto wire = connectionAt(screen);
    juce::PopupMenu menu;
    if (viewport().contains(screen.toInt()))
        menu.addItem(1, "Create routing draft here (structural only, no DSP)");
    std::vector<domain::GraphId> wires;
    if (endpoint)
    {
        for (auto id : scene_->incident(endpoint->node))
        {
            const auto& connection = workspace_.graph().connections.at(id);
            if (connection.source == *endpoint || connection.destination == *endpoint)
            {
                wires.push_back(id);
                menu.addItem(100 + static_cast<int>(wires.size()), "Disconnect wire " + juce::String(id));
            }
        }
    }
    else if (wire != 0 && node == 0)
    {
        wires.push_back(wire);
        menu.addItem(101, "Disconnect selected wire");
    }
    if (node != 0 && !endpoint && !fixedEndpoint(node))
    {
        if (!selected_.contains(node))
            selected_ = {node};
        selectedWire_ = 0;
        focusedNode_ = node;
        menu.addItem(2, "Delete selected nodes and incident wires");
    }
    menu.addSeparator();
    menu.addItem(3, "Select all nodes");
    if (viewport().contains(screen.toInt()))
        menu.addItem(4, "Fit graph");
    updateDetails();
    repaint(viewport());
    const juce::Component::SafePointer<CanvasView> safe(this);
    const auto anchor = localPointToGlobal(screen.toInt());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({anchor.x, anchor.y, 1, 1})
                          .withParentComponent(getTopLevelComponent()),
        [safe, screen, wires](int result)
        {
            if (!safe)
                return;
            if (result == 1) safe->createNodeAt(screen);
            else if (result == 2) safe->deleteSelection();
            else if (result == 3) safe->selectAll();
            else if (result == 4) safe->fitGraph();
            else if (result >= 101 && result < 101 + static_cast<int>(wires.size()))
                safe->finish(safe->workspace_.disconnect(wires[static_cast<std::size_t>(result - 101)]));
        });
}

void CanvasView::endpointMenu(domain::GraphId id)
{
    const auto found = workspace_.graph().endpoints.find(id);
    if (found == workspace_.graph().endpoints.end()) return;
    const auto endpoint = found->second;
    const auto channels = endpointChannels ? endpointChannels(endpoint.role) : std::vector<std::string>{};
    const auto selectedChannels = selectedEndpointChannels ? selectedEndpointChannels(id) : std::vector<int>{};
    const auto selected = [&](int index)
    {
        return std::find(selectedChannels.begin(), selectedChannels.end(), index) != selectedChannels.end();
    };
    juce::PopupMenu menu, mono, stereo, advanced;
    for (int i = 0; i < static_cast<int>(channels.size()); ++i)
    {
        const auto label = juce::String(i + 1) + juce::String::fromUTF8(" · ")
            + juce::String::fromUTF8(channels[static_cast<std::size_t>(i)].c_str());
        mono.addItem(10000 + i, label, true, selectedChannels.size() == 1 && selected(i));
        if (i + 1 < static_cast<int>(channels.size()))
            stereo.addItem(20000 + i, label + " + " + juce::String(i + 2) + juce::String::fromUTF8(" · ")
                + juce::String::fromUTF8(channels[static_cast<std::size_t>(i + 1)].c_str()),
                true, selectedChannels.size() == 2 && selected(i) && selected(i + 1));
        advanced.addItem(30000 + i, label, true, selected(i));
    }
    menu.addSectionHeader(juce::String::fromUTF8(endpoint.name.c_str()));
    menu.addSubMenu("Mono channel", mono);
    menu.addSubMenu("Adjacent stereo pair", stereo);
    menu.addSubMenu(juce::String::fromUTF8("Advanced · choose any channels"), advanced);
    menu.addItem(1, "Unassign channels", true, endpoint.channelNames.empty());
    if (channels.empty())
        menu.addItem(6, "Select and inspect a device in Audio Setup first", false);
    menu.addSeparator();
    menu.addItem(2, endpoint.enabled ? "Disable row" : "Enable row");
    menu.addItem(3, "Rename row...");
    menu.addItem(4, "Remove row");
    const juce::Component::SafePointer<CanvasView> safe(this);
    const auto anchor = localPointToGlobal(endpointCardBounds(id).getCentre());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({anchor.x, anchor.y, 1, 1})
                          .withParentComponent(getTopLevelComponent()),
        [safe, id, endpoint, selectedChannels](int result)
        {
            if (!safe) return;
            if (result == 1)
            {
                if (safe->assignEndpointChannels)
                    safe->finish(safe->assignEndpointChannels(id, {}));
            }
            else if (result >= 10000 && result < 11024)
            {
                if (safe->assignEndpointChannels)
                    safe->finish(safe->assignEndpointChannels(id, {result - 10000}));
            }
            else if (result >= 20000 && result < 21024)
            {
                if (safe->assignEndpointChannels)
                    safe->finish(safe->assignEndpointChannels(id, {result - 20000, result - 20000 + 1}));
            }
            else if (result >= 30000 && result < 31024)
            {
                auto next = selectedChannels;
                const auto channel = result - 30000;
                if (const auto current = std::find(next.begin(), next.end(), channel); current != next.end())
                    next.erase(current);
                else if (next.size() < 32)
                    next.push_back(channel);
                std::sort(next.begin(), next.end());
                if (safe->assignEndpointChannels)
                    safe->finish(safe->assignEndpointChannels(id, next));
            }
            else if (result == 2)
                safe->finish(safe->workspace_.editHardwareEndpoint(id, endpoint.name, !endpoint.enabled,
                    endpoint.layoutChannels, endpoint.channelNames));
            else if (result == 4)
                safe->finish(safe->workspace_.removeHardwareEndpoint(id));
            else if (result == 3)
            {
                auto* alert = new juce::AlertWindow("Rename endpoint row", "Keep its channel assignment; change only the friendly name.",
                                                     juce::AlertWindow::NoIcon);
                alert->addTextEditor("name", juce::String::fromUTF8(endpoint.name.c_str()), "Row name");
                alert->addButton("Cancel", 0);
                alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
                alert->enterModalState(true, juce::ModalCallbackFunction::create(
                    [safe, id, endpoint, alert](int accepted)
                    {
                        if (!safe || accepted != 1) return;
                        safe->finish(safe->workspace_.editHardwareEndpoint(id, alert->getTextEditorContents("name").toStdString(),
                            endpoint.enabled, endpoint.layoutChannels, endpoint.channelNames));
                    }), true);
            }
        });
}
}
