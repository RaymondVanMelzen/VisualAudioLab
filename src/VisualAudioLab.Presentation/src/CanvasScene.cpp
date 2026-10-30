#include <val/presentation/CanvasScene.h>
#include <cmath>

namespace val::presentation
{
std::int64_t CanvasIndex::key(int x, int y)
{
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
                                     | static_cast<std::uint32_t>(y));
}

void CanvasIndex::add(domain::GraphId id, juce::Rectangle<float> bounds)
{
    for (int x = static_cast<int>(std::floor(bounds.getX() / cellSize));
         x <= static_cast<int>(std::floor(bounds.getRight() / cellSize)); ++x)
        for (int y = static_cast<int>(std::floor(bounds.getY() / cellSize));
             y <= static_cast<int>(std::floor(bounds.getBottom() / cellSize)); ++y)
            cells_[key(x, y)].push_back(id);
}

void CanvasIndex::add(domain::GraphId id, const juce::Path& path)
{
    juce::PathFlatteningIterator iterator(path, {}, 1.0f);
    while (iterator.next())
    {
        const juce::Point<float> start{iterator.x1, iterator.y1}, end{iterator.x2, iterator.y2};
        const auto steps = juce::jmax(1, static_cast<int>(std::ceil(start.getDistanceFrom(end) / 128.0f)));
        for (int step = 0; step < steps; ++step)
        {
            const auto a = start + (end - start) * (static_cast<float>(step) / steps);
            const auto b = start + (end - start) * (static_cast<float>(step + 1) / steps);
            add(id, juce::Rectangle<float>(a, b).expanded(2.0f));
        }
    }
}

std::set<domain::GraphId> CanvasIndex::query(juce::Rectangle<float> bounds) const
{
    std::set<domain::GraphId> result;
    const int left = static_cast<int>(std::floor(bounds.getX() / cellSize));
    const int right = static_cast<int>(std::floor(bounds.getRight() / cellSize));
    const int top = static_cast<int>(std::floor(bounds.getY() / cellSize));
    const int bottom = static_cast<int>(std::floor(bounds.getBottom() / cellSize));
    // Fit-all and rubber-band can span huge empty areas. Iterate occupied cells instead.
    const auto cellCount = (static_cast<std::int64_t>(right) - left + 1)
                         * (static_cast<std::int64_t>(bottom) - top + 1);
    if (cellCount > static_cast<std::int64_t>(cells_.size()) * 2)
    {
        for (const auto& [cell, ids] : cells_)
        {
            const int x = static_cast<std::int32_t>(static_cast<std::uint64_t>(cell) >> 32);
            const int y = static_cast<std::int32_t>(static_cast<std::uint64_t>(cell) & 0xffffffffu);
            if (x >= left && x <= right && y >= top && y <= bottom)
                result.insert(ids.begin(), ids.end());
        }
    }
    else
    {
        for (int x = left; x <= right; ++x)
            for (int y = top; y <= bottom; ++y)
                if (const auto found = cells_.find(key(x, y)); found != cells_.end())
                    result.insert(found->second.begin(), found->second.end());
    }
    return result;
}

CanvasScene::CanvasScene(const domain::Graph& graph)
{
    bool first = true;
    for (const auto& [id, node] : graph.nodes)
    {
        const auto rectangle = NodeView::bounds(node.position);
        nodes_.add(id, rectangle.expanded(8.0f));
        bounds_ = first ? rectangle : bounds_.getUnion(rectangle);
        first = false;
    }
    for (const auto& [id, wire] : graph.connections)
    {
        ConnectionView view(NodeView::port(graph.nodes.at(wire.source.node).position, wire.source.port),
                             NodeView::port(graph.nodes.at(wire.destination.node).position, wire.destination.port));
        connections_.add(id, view.path());
        wires_.emplace(id, std::move(view));
        incident_[wire.source.node].push_back(id);
        incident_[wire.destination.node].push_back(id);
    }
}

const std::vector<domain::GraphId>& CanvasScene::incident(domain::GraphId node) const
{
    static const std::vector<domain::GraphId> empty;
    if (const auto found = incident_.find(node); found != incident_.end())
        return found->second;
    return empty;
}
}
