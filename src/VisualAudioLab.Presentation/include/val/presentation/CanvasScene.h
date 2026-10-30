#pragma once

#include "NodeView.h"
#include "ConnectionView.h"
#include <set>
#include <unordered_map>

namespace val::presentation
{
/**
 * Uniform patch-space grid for broad-phase node/cable queries.
 * Cable segments, not their potentially enormous enclosing rectangles, populate cells.
 * Queries deduplicate candidates; exact geometry is tested by the caller.
 */
class CanvasIndex final
{
public:
    void add(domain::GraphId id, juce::Rectangle<float> bounds);
    void add(domain::GraphId id, const juce::Path& path);
    [[nodiscard]] std::set<domain::GraphId> query(juce::Rectangle<float> bounds) const;

private:
    static constexpr int cellSize = 256;
    static std::int64_t key(int x, int y);
    std::unordered_map<std::int64_t, std::vector<domain::GraphId>> cells_;
};

/**
 * Rebuilt once after accepted content edits; never owns or changes domain content.
 * Caches cable paths, adjacency and spatial indexes. Paint/hit-test visit viewport candidates,
 * while a drag previews only moved nodes and their incident wires without rebuilding the scene.
 */
class CanvasScene final
{
public:
    explicit CanvasScene(const domain::Graph& graph);
    [[nodiscard]] std::set<domain::GraphId> nodes(juce::Rectangle<float> rectangle) const
    {
        return nodes_.query(rectangle);
    }
    [[nodiscard]] std::set<domain::GraphId> connections(juce::Rectangle<float> rectangle) const
    {
        return connections_.query(rectangle);
    }
    [[nodiscard]] const std::map<domain::GraphId, ConnectionView>& wires() const noexcept { return wires_; }
    [[nodiscard]] const std::vector<domain::GraphId>& incident(domain::GraphId node) const;
    [[nodiscard]] juce::Rectangle<float> contentBounds() const noexcept { return bounds_; }

private:
    CanvasIndex nodes_, connections_;
    std::map<domain::GraphId, ConnectionView> wires_;
    std::unordered_map<domain::GraphId, std::vector<domain::GraphId>> incident_;
    juce::Rectangle<float> bounds_;
};
}
