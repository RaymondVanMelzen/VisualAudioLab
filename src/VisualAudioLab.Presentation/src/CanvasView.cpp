#include <val/presentation/CanvasView.h>
#include <val/presentation/ShellTheme.h>
#include <cmath>

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
             &add_, &remove_, &fit_, &minus_, &plus_, &zoomLabel_, &help_})
        addAndMakeVisible(child);
    for (auto* button : {&add_, &remove_, &fit_, &minus_, &plus_})
        button->setWantsKeyboardFocus(false);
    help_.setText("N: add | Drag ports: wire | Wheel: zoom | Middle/Space-drag: pan",
                   juce::dontSendNotification);
    help_.setTooltip("Shift adds selection; Ctrl/Command toggles. Empty drag selects a box. "
                     "Delete removes selection. M then arrows/Enter moves; Escape cancels. "
                     "Arrows navigate nodes; Tab leaves canvas. C then arrows/Enter connects; D disconnects input. "
                     "Right-click a wire to disconnect. Ctrl/Command+Z undoes.");
    synchronise();
}

CanvasView::~CanvasView() = default;

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
    return getLocalBounds().withTrimmedTop(34).withTrimmedBottom(26);
}

void CanvasView::resized()
{
    auto row = getLocalBounds().removeFromTop(32).reduced(2);
    add_.setBounds(row.removeFromLeft(110));
    remove_.setBounds(row.removeFromLeft(65));
    fit_.setBounds(row.removeFromLeft(42));
    minus_.setBounds(row.removeFromLeft(28));
    zoomLabel_.setBounds(row.removeFromLeft(55));
    plus_.setBounds(row.removeFromLeft(28));
    help_.setBounds(getLocalBounds().removeFromBottom(24));
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
    juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(viewport());
    const auto visible = visiblePatch();
    const auto clip = graphics.getClipBounds().getIntersection(viewport()).toFloat();
    const juce::Rectangle<float> damaged{toPatch(clip.getTopLeft()), toPatch(clip.getBottomRight())};
    const auto origin = toScreen({0, 0});
    graphics.addTransform(juce::AffineTransform::scale(zoom_).translated(origin.x, origin.y));
    float grid = 32.0f;
    while (grid * zoom_ < 20.0f)
        grid *= 4.0f;
    graphics.setColour(juce::Colour(0xff2a3442));
    for (float x = std::floor(damaged.getX() / grid) * grid; x < damaged.getRight(); x += grid)
        for (float y = std::floor(damaged.getY() / grid) * grid; y < damaged.getBottom(); y += grid)
            graphics.fillRect(x, y, 1.0f / zoom_, 1.0f / zoom_);

    for (auto id : scene_->connections(damaged.expanded(8.0f / zoom_)))
        if (!movingWires_.contains(id))
            scene_->wires().at(id).paint(graphics, id == selectedWire_);
    for (auto id : movingWires_)
        wireView(workspace_.graph().connections.at(id)).paint(graphics, id == selectedWire_);

    auto candidates = scene_->nodes(damaged.expanded(12.0f / zoom_));
    for (const auto& [id, point] : preview_)
        if (NodeView::bounds(point).intersects(visible))
            candidates.insert(id);
    for (auto id : candidates)
    {
        const auto point = position(id);
        if (NodeView::bounds(point).expanded(12.0f / zoom_).intersects(visible))
        {
            NodeView::paint(graphics, workspace_.graph().nodes.at(id), point,
                            selected_.contains(id), focusedNode_ == id && hasKeyboardFocus(true), zoom_ >= 0.35f);
            if (paintObservation && zoom_ >= 0.75f)
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
        graphics.setColour(ShellTheme::accent().withAlpha(0.12f));
        graphics.fillRect(marquee_);
        graphics.setColour(ShellTheme::accent());
        graphics.drawRect(marquee_, 1.0f / zoom_);
    }
    if (workspace_.graph().nodes.empty())
    {
        graphics.setColour(juce::Colours::lightgrey);
        graphics.drawFittedText("Place module instances from Module Browser.\nRouting Draft (N) creates a structural utility, not an audio function.\n"
                                "No DSP or audio processing.",
                                visible.toNearestInt().reduced(8), juce::Justification::centred, 4);
    }
}

bool CanvasView::observationVisible(domain::GraphId id) const
{
    if (const auto* peer = getPeer(); peer && peer->isMinimised()) return false;
    const auto found = workspace_.graph().nodes.find(id);
    return isShowing() && zoom_ >= 0.75f && found != workspace_.graph().nodes.end()
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
    setDescription(juce::String(workspace_.graph().nodes.size()) + " placed nodes (module instances or routing utilities), "
                    + juce::String(selected_.size()) + " selected. Focused node "
                    + juce::String(focusedNode_) + ". " + help_.getTooltip());
}

void CanvasView::synchronise()
{
    cancelGesture();
    const auto& graph = workspace_.graph();
    std::erase_if(selected_, [&](auto id) { return !graph.nodes.contains(id); });
    if (!graph.nodes.contains(focusedNode_))
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
        finish(workspace_.deleteNodes({selected_.begin(), selected_.end()}));
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
        (void) node;
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
    if (!workspace_.graph().nodes.empty() && !viewport().isEmpty())
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
    const auto point = toPatch(screen);
    const auto candidates = scene_->nodes({point.x - 1, point.y - 1, 2, 2});
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
        if (NodeView::bounds(position(*it)).contains(point))
            return *it;
    return 0;
}

std::optional<domain::Endpoint> CanvasView::portAt(juce::Point<float> screen) const
{
    const auto point = toPatch(screen);
    const float tolerance = 9.0f / zoom_;
    const auto candidates = scene_->nodes({point.x - tolerance, point.y - tolerance,
                                           tolerance * 2, tolerance * 2});
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
        for (const auto direction : {domain::Port::input, domain::Port::output})
            if (NodeView::port(position(*it), direction).getDistanceFrom(point) <= tolerance)
                return domain::Endpoint{*it, direction};
    return std::nullopt;
}

domain::GraphId CanvasView::connectionAt(juce::Point<float> screen) const
{
    const auto point = toPatch(screen);
    const auto tolerance = 7.0f / zoom_;
    for (auto id : scene_->connections({point.x - tolerance, point.y - tolerance,
                                        tolerance * 2, tolerance * 2}))
        if (scene_->wires().at(id).hit(point, tolerance))
            return id;
    return 0;
}

void CanvasView::selectRectangle(juce::Rectangle<float> rectangle, bool add, bool toggle)
{
    if (!add && !toggle)
        selected_.clear();
    for (auto id : scene_->nodes(rectangle))
        if (NodeView::bounds(position(id)).intersects(rectangle))
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
    if (!viewport().contains(event.position.toInt()))
        return;
    grabKeyboardFocus();
    cancelGesture();
    down_ = pointer_ = event.position;
    if (event.mods.isPopupMenu())
    {
        contextMenu(event.position);
        return;
    }
    if (event.mods.isMiddleButtonDown() || spaceHeld_)
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
        if (selected_.contains(node))
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
        const auto point = toScreen(NodeView::bounds(next->second.position).getCentre());
        if (!viewport().contains(point.toInt()))
            panBy(viewport().getCentre().toFloat() - point);
        updateDetails();
        repaint(viewport());
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
    if (node != 0 && !endpoint)
    {
        if (!selected_.contains(node))
            selected_ = {node};
        selectedWire_ = 0;
        focusedNode_ = node;
        menu.addItem(2, "Delete selected nodes and incident wires");
    }
    menu.addSeparator();
    menu.addItem(3, "Select all nodes");
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
}
