#include "TestSupport.h"
#include <val/presentation/CanvasView.h>
#include <chrono>
#include <iomanip>

namespace
{
/** Synthesizes component-local pointer events to exercise the real gesture handlers. */
juce::MouseEvent pointerEvent(val::presentation::CanvasView& canvas, juce::Point<float> point,
                              juce::Point<float> down, int modifiers, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys(modifiers),
            1.0f, 0, 0, 0, 0, &canvas, &canvas, now, down, now, 1, dragged};
}

void click(val::presentation::CanvasView& canvas, juce::Point<float> point, int modifiers = 0)
{
    const auto event = pointerEvent(canvas, point, point, modifiers | juce::ModifierKeys::leftButtonModifier);
    canvas.mouseDown(event);
    canvas.mouseUp(event);
}

void drag(val::presentation::CanvasView& canvas, juce::Point<float> from, juce::Point<float> to,
          int modifiers = juce::ModifierKeys::leftButtonModifier)
{
    canvas.mouseDown(pointerEvent(canvas, from, from, modifiers));
    canvas.mouseDrag(pointerEvent(canvas, to, from, modifiers, true));
    canvas.mouseUp(pointerEvent(canvas, to, from, modifiers, true));
}

double elapsed(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}

void runNodeEditorTests()
{
    using namespace val;
    using namespace presentation;
    application::WorkspaceService service;
    require(service.createNode({0, 0}, "A").succeeded, "Create A.");
    const auto a = service.lastCreatedNode();
    require(service.createNode({350, 0}, "B").succeeded, "Create B.");
    const auto b = service.lastCreatedNode();
    require(service.createNode({700, 200}, "C").succeeded, "Create C.");
    const auto c = service.lastCreatedNode();
    CanvasView canvas(service);
    canvas.setSize(1200, 700);
    int errors{};
    canvas.onEdit = [&](const auto& result) { if (!result.succeeded) ++errors; };
    const auto body = [&](domain::GraphId id)
    {
        return canvas.toScreen(NodeView::bounds(service.graph().nodes.at(id).position).getCentre());
    };
    const auto port = [&](domain::GraphId id, domain::Port direction)
    {
        return canvas.toScreen(NodeView::port(service.graph().nodes.at(id).position, direction));
    };
    require(canvas.nodeAt(body(a)) == a, "Node hit testing uses world coordinates.");
    require(canvas.portAt(port(a, domain::Port::output)) == domain::Endpoint{a, domain::Port::output},
            "Ports have stable endpoint identities.");
    click(canvas, body(a));
    click(canvas, body(b), juce::ModifierKeys::shiftModifier);
    require(canvas.selection() == std::set<domain::GraphId>{a, b}, "Shift adds selection.");
    click(canvas, body(a), juce::ModifierKeys::commandModifier);
    require(canvas.selection() == std::set<domain::GraphId>{b}, "Primary modifier toggles selection.");
    canvas.selectRectangle({-10, -10, 550, 120}, false, false);
    require(canvas.selection() == std::set<domain::GraphId>{a, b},
            "Rubber-band intersects node bodies and excludes distant nodes.");
    const auto original = service.graph();
    const auto start = body(a), end = start + juce::Point<float>{36, 44};
    canvas.mouseDown(pointerEvent(canvas, start, start, juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, end, start, juce::ModifierKeys::leftButtonModifier, true));
    require(service.graph() == original, "Dragging previews without mutating document content.");
    canvas.mouseUp(pointerEvent(canvas, end, start, juce::ModifierKeys::leftButtonModifier, true));
    require(service.graph().nodes.at(a).position == domain::Position{36, 44}
            && service.graph().nodes.at(b).position == domain::Position{386, 44},
            "All selected nodes share one world-space delta.");
    require(service.undo().succeeded && service.graph() == original,
            "One history entry represents an entire multi-node gesture.");
    canvas.synchronise();
    require(service.redo().succeeded, "Redo the complete gesture.");
    canvas.synchronise();
    const auto moved = service.graph();
    canvas.mouseDown(pointerEvent(canvas, body(a), body(a), juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, body(a) + juce::Point<float>{20, 20}, body(a),
                                   juce::ModifierKeys::leftButtonModifier, true));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    require(service.graph() == moved && service.undo().succeeded && service.graph() == original,
            "Escape discards the preview and creates no undo entry.");
    canvas.synchronise();
    canvas.mouseDown(pointerEvent(canvas, body(a), body(a), juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, body(a) + juce::Point<float>{10, 10}, body(a),
                                   juce::ModifierKeys::leftButtonModifier, true));
    canvas.focusLost(juce::Component::focusChangedDirectly);
    require(service.graph() == original, "Losing canvas focus cancels an uncommitted move.");
    const auto anchor = juce::Point<float>{420, 290};
    const auto patchAnchor = canvas.toPatch(anchor);
    canvas.zoomAt(2.0f, anchor);
    require(canvas.toPatch(anchor).getDistanceFrom(patchAnchor) < 0.001f,
            "Zoom preserves the graph point under its screen anchor.");
    canvas.panBy({-40, 35});
    const auto point = juce::Point<float>{145, -30};
    require(canvas.toPatch(canvas.toScreen(point)).getDistanceFrom(point) < 0.001f,
            "Screen/patch conversion round trips after zoom and pan.");
    canvas.zoomAt(999, anchor);
    require(canvas.zoom() == 3.0f, "Zoom is bounded above.");
    canvas.zoomAt(0.001f, anchor);
    require(canvas.zoom() == 0.025f, "Zoom is bounded below.");
    canvas.zoomAt(1.0f, anchor);
    canvas.fitGraph();
    require(service.graph() == original && service.canRedo(), "Viewport navigation preserves content and redo.");

    drag(canvas, port(a, domain::Port::output), port(b, domain::Port::input));
    require(service.graph().connections.size() == 1, "Port dragging creates an accepted wire.");
    const auto edge = service.graph().connections.begin()->first;
    const ConnectionView cable(NodeView::port(service.graph().nodes.at(a).position, domain::Port::output),
                               NodeView::port(service.graph().nodes.at(b).position, domain::Port::input));
    const auto midpoint = cable.path().getPointAlongPath(cable.path().getLength() * 0.5f);
    require(canvas.connectionAt(canvas.toScreen(midpoint)) == edge,
            "Cable hit testing measures geometric distance, not distance along the curve.");
    require(canvas.connectionAt(canvas.toScreen(midpoint + juce::Point<float>{0, 40})) == 0,
            "Cable hit testing rejects points outside its screen-space tolerance.");
    click(canvas, canvas.toScreen(midpoint));
    canvas.deleteSelection();
    require(service.graph().connections.empty() && service.undo().succeeded
            && service.graph().connections.begin()->first == edge,
            "Wire selection/delete and undo restore its stable identity.");
    canvas.synchronise();
    drag(canvas, port(a, domain::Port::output), port(b, domain::Port::input));
    require(errors == 1 && service.graph().connections.size() == 1,
            "Rejected duplicate wiring surfaces an error without adding a wire.");
    drag(canvas, port(b, domain::Port::output), port(c, domain::Port::input));
    require(service.graph().connections.size() == 2, "Additional wiring works after rejection.");
    const auto connected = service.graph();
    canvas.selectRectangle({-10, -10, 550, 120}, false, false);
    canvas.deleteSelection();
    require(service.graph().nodes.size() == 1 && service.graph().connections.empty(),
            "Canvas deletion removes selected nodes and every incident wire.");
    require(service.undo().succeeded && service.graph() == connected,
            "Undo deletion restores geometry and incident topology.");
    canvas.synchronise();
    const auto selectedBefore = canvas.selection();
    const auto marqueeStart = canvas.toScreen({-20, -20});
    canvas.mouseDown(pointerEvent(canvas, marqueeStart, marqueeStart, juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, body(c) + juce::Point<float>{100, 100}, marqueeStart,
                                   juce::ModifierKeys::leftButtonModifier, true));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    require(canvas.selection() == selectedBefore, "Cancelled marquee restores prior node selection.");
    const auto descriptionBefore = canvas.getDescription();
    canvas.mouseDown(pointerEvent(canvas, marqueeStart, marqueeStart, juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, body(c) + juce::Point<float>{100, 100}, marqueeStart,
                                   juce::ModifierKeys::leftButtonModifier, true));
    require(!canvas.selection().empty(), "Marquee previews a non-empty selection before losing focus.");
    canvas.focusLost(juce::Component::focusChangedDirectly);
    require(canvas.selection() == selectedBefore && canvas.getDescription() == descriptionBefore,
            "Focus cancellation restores selection and its exposed control/accessibility state.");
    drag(canvas, canvas.toScreen({-20, -20}), canvas.toScreen({550, 150}));
    require(canvas.selection() == std::set<domain::GraphId>{a, b},
            "Actual marquee pointer handlers select by patch-space intersection.");

    click(canvas, body(a));
    canvas.keyPressed(juce::KeyPress('m'));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    require(service.graph() == connected, "Keyboard move also remains a preview until Enter.");
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    require(service.graph().nodes.at(a).position == domain::Position{16, 16}, "Keyboard move commits once.");
    require(service.undo().succeeded && service.graph() == connected, "Undo keyboard movement is exact.");
    canvas.synchronise();
    require(!canvas.keyPressed(juce::KeyPress(juce::KeyPress::tabKey)), "Tab can leave the canvas; no focus trap.");
    const auto offsetBefore = canvas.toScreen({0, 0});
    drag(canvas, {800, 450}, {850, 480}, juce::ModifierKeys::middleButtonModifier);
    require(canvas.toScreen({0, 0}).getDistanceFrom(offsetBefore + juce::Point<float>{50, 30}) < 0.001f,
            "Middle drag pans in screen pixels independent of zoom.");
    require(service.graph() == connected, "Selection, zoom and pan never mutate the graph.");

    // Broad-phase cells must work at negative coordinates and for a long backwards cable.
    domain::Graph negative;
    negative.nodes.emplace(1, domain::Node{1, "Negative", {-900, -600}});
    negative.nodes.emplace(2, domain::Node{2, "Far", {9000, 6000}});
    negative.connections.emplace(3, domain::Connection{3, {2, domain::Port::output}, {1, domain::Port::input}});
    const CanvasScene negativeScene(negative);
    require(negativeScene.nodes({-910, -610, 190, 110}).contains(1), "Negative grid coordinates are stable.");
    const auto& longWire = negativeScene.wires().at(3);
    for (int sample = 0; sample <= 20; ++sample)
    {
        const auto pointOnWire = longWire.path().getPointAlongPath(longWire.path().getLength() * sample / 20.0f);
        require(negativeScene.connections({pointOnWire.x - 4, pointOnWire.y - 4, 8, 8}).contains(3),
                "Long and backwards cable segments are spatially indexed without missed hits.");
    }

    click(canvas, body(c));
    canvas.keyPressed(juce::KeyPress('d'));
    require(service.graph().connections.size() == 1, "Keyboard Disconnect removes focused node's incoming wire.");
    require(service.undo().succeeded, "Undo keyboard disconnect.");
    canvas.synchronise();
    const auto beforeCreation = service.graph().nodes.size();
    canvas.createNodeAt({600, 450});
    require(service.graph().nodes.size() == beforeCreation + 1
            && canvas.selection().contains(service.lastCreatedNode()), "Insertion focuses/selects the new node.");
    const auto d = service.lastCreatedNode();
    click(canvas, body(c));
    canvas.keyPressed(juce::KeyPress('c'));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    bool connectedToNew{};
    for (const auto& [id, wire] : service.graph().connections)
    {
        (void) id;
        connectedToNew |= wire.source.node == c && wire.destination.node == d;
    }
    require(connectedToNew, "Keyboard connect uses stable source/destination IDs.");
    const auto atD = body(d);
    const auto inputA = port(a, domain::Port::input);
    canvas.mouseDown(pointerEvent(canvas, port(d, domain::Port::output), port(d, domain::Port::output),
                                   juce::ModifierKeys::leftButtonModifier));
    canvas.mouseDrag(pointerEvent(canvas, inputA, port(d, domain::Port::output),
                                   juce::ModifierKeys::leftButtonModifier, true));
    const auto beforeCancelledWire = service.graph();
    canvas.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    canvas.mouseUp(pointerEvent(canvas, inputA, atD, juce::ModifierKeys::leftButtonModifier, true));
    require(service.graph() == beforeCancelledWire, "Escape cancels provisional wiring before validation/commit.");
    const auto beforeKeyboardInsert = service.graph();
    require(canvas.keyPressed(juce::KeyPress('n')), "Native lowercase letter events activate Add Node.");
    require(service.graph().nodes.size() == beforeKeyboardInsert.nodes.size() + 1,
            "Lowercase N inserts through the same command path.");
    require(canvas.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
            && service.graph() == beforeKeyboardInsert, "Native lowercase Ctrl/Command+Z undoes.");
    require(canvas.keyPressed(juce::KeyPress('Z',
            juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
            && service.graph().nodes.size() == beforeKeyboardInsert.nodes.size() + 1,
            "Shifted native redo events also use the shared history.");
    const auto snapshot = canvas.createComponentSnapshot(canvas.getLocalBounds());
    require(snapshot.isValid(), "Canvas renders actual structural nodes and connections.");
}

void runLargeGraphBenchmark()
{
    using namespace val;
    using namespace presentation;
    constexpr int count = 5000, columns = 100;
    application::WorkspaceService service;
    std::vector<domain::GraphId> ids;
    const auto createStart = std::chrono::steady_clock::now();
    for (int index = 0; index < count; ++index)
    {
        require(service.createNode({(index % columns) * 240.0, (index / columns) * 150.0}).succeeded,
                "Large fixture node insertion.");
        ids.push_back(service.lastCreatedNode());
    }
    const auto creationMs = elapsed(createStart);
    const auto connectStart = std::chrono::steady_clock::now();
    for (int index = 0; index < count; ++index)
        if (index % columns != columns - 1)
            require(service.connect({ids[index], domain::Port::output},
                                     {ids[index + 1], domain::Port::input}).succeeded, "Large fixture connection.");
    const auto connectionMs = elapsed(connectStart);
    const auto sceneStart = std::chrono::steady_clock::now();
    const CanvasScene scene(service.graph());
    const auto sceneMs = elapsed(sceneStart);
    std::size_t maxNodes{}, maxWires{}, checksum{};
    const auto queryStart = std::chrono::steady_clock::now();
    for (int query = 0; query < 200; ++query)
    {
        const juce::Rectangle<float> rectangle{static_cast<float>((query % 20) * 900),
                                               static_cast<float>((query % 7) * 800), 1200, 700};
        const auto nodes = scene.nodes(rectangle), wires = scene.connections(rectangle);
        maxNodes = juce::jmax(maxNodes, nodes.size());
        maxWires = juce::jmax(maxWires, wires.size());
        checksum += nodes.size() + wires.size();
        for (const auto& [id, node] : service.graph().nodes)
            if (NodeView::bounds(node.position).intersects(rectangle))
                require(nodes.contains(id), "Spatial query must never omit an intersecting node.");
    }
    const auto queryMs = elapsed(queryStart);
    require(maxNodes < 150 && maxWires < 150, "Viewport queries cull a 5,000-node graph to bounded local candidates.");
    const auto indexedStart = std::chrono::steady_clock::now();
    for (int query = 0; query < 200; ++query)
    {
        const juce::Rectangle<float> rectangle{static_cast<float>((query % 20) * 900),
                                               static_cast<float>((query % 7) * 800), 1200, 700};
        checksum += scene.nodes(rectangle).size() + scene.connections(rectangle).size();
    }
    const auto indexedMs = elapsed(indexedStart);
    for (const auto& [id, wire] : scene.wires())
        for (int sample = 0; sample <= 4; ++sample)
        {
            const auto point = wire.path().getPointAlongPath(wire.path().getLength() * sample / 4.0f);
            require(scene.connections({point.x - 3, point.y - 3, 6, 6}).contains(id),
                    "Cable grid never omits sampled points along cached curves.");
        }
    const auto canvasStart = std::chrono::steady_clock::now();
    CanvasView canvas(service);
    canvas.setSize(1200, 800);
    const auto canvasMs = elapsed(canvasStart);
    const auto paintStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 20; ++frame)
    {
        canvas.panBy({-24, 0});
        require(canvas.createComponentSnapshot(canvas.getLocalBounds(), true, 1.0f,
                                                juce::SoftwareImageType{}).isValid(), "Large actual canvas renders.");
    }
    const auto paintMs = elapsed(paintStart);
    canvas.fitGraph();
    const auto fitStart = std::chrono::steady_clock::now();
    require(canvas.createComponentSnapshot(canvas.getLocalBounds(), true, 1.0f,
                                            juce::SoftwareImageType{}).isValid(), "Full graph overview renders.");
    const auto fitMs = elapsed(fitStart);
    require(canvas.toScreen(scene.contentBounds().getTopLeft()).x >= canvas.viewport().getX()
            && canvas.toScreen(scene.contentBounds().getBottomRight()).x <= canvas.viewport().getRight(),
            "The 5,000-node fixture actually fits in the overview.");
    const auto moveStart = std::chrono::steady_clock::now();
    require(service.moveNodes({{ids[100], {500, 500}}, {ids[101], {740, 500}}}).succeeded,
            "Large fixture move selection.");
    require(service.undo().succeeded && service.redo().succeeded, "Large graph move undo/redo.");
    const auto moveMs = elapsed(moveStart);
    const auto deleteStart = std::chrono::steady_clock::now();
    const auto original = service.graph();
    require(service.deleteNodes({ids[100], ids[101]}).succeeded && service.undo().succeeded
            && service.graph() == original, "Large graph deletion restores exact incident topology.");
    const auto deleteMs = elapsed(deleteStart);
    std::cout << std::fixed << std::setprecision(3)
              << "Fixture: " << count << " nodes, " << service.graph().connections.size()
              << " wires, 100 columns, spacing 240x150 patch units\n"
              << "Create nodes ms: " << creationMs << "\nConnect total ms: " << connectionMs
              << "\nScene/index build ms: " << sceneMs << "\nCanvas construction ms: " << canvasMs
              << "\n200 query + brute-force verification ms: " << queryMs
              << "\n200 indexed queries only ms: " << indexedMs
              << "\nMax viewport candidates: " << maxNodes << " nodes, " << maxWires << " wires"
              << "\n20 software snapshot paints ms: " << paintMs << " (mean " << paintMs / 20 << ")"
              << "\nFit-all software snapshot ms: " << fitMs
              << "\nTwo-node move + undo + redo ms: " << moveMs
              << "\nDelete + undo + full equality check ms: " << deleteMs
              << "\nCandidate checksum: " << checksum << '\n';
}
