#include <val/application/WorkspaceService.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace val::application
{
namespace
{
bool validPosition(domain::Position position)
{
    return std::isfinite(position.x) && std::isfinite(position.y)
        && std::abs(position.x) <= 1000000.0 && std::abs(position.y) <= 1000000.0;
}

/**
 * Exchanges prepared map node handles for just the affected IDs, without allocating during
 * execute/undo. Deleted nodes and incident wires remain owned by history until discarded.
 * The workspace graph outlives this command. Ordered map insertion cannot throw with these
 * integral keys and equal allocators; all allocations happen before history execution.
 */
class GraphEdit final : public Command
{
public:
    GraphEdit(domain::Graph& graph, std::string label, domain::Graph alternate,
              std::vector<domain::GraphId> nodes, std::vector<domain::GraphId> connections,
              domain::GraphId& activePatch)
        : graph_(graph), label_(std::move(label)), alternate_(std::move(alternate)),
          nodes_(std::move(nodes)), connections_(std::move(connections)),
          activePatch_(activePatch), patch_(activePatch) {}

    std::string_view name() const noexcept override { return label_; }
    Result execute() override
    {
        exchange(graph_.nodes, alternate_.nodes, nodes_);
        exchange(graph_.connections, alternate_.connections, connections_);
        activePatch_ = patch_;
        return Result::success();
    }
    Result undo() override { return execute(); }

private:
    template <typename Map>
    static void exchange(Map& accepted, Map& alternate, const std::vector<domain::GraphId>& ids)
    {
        for (auto id : ids)
        {
            auto before = accepted.extract(id);
            auto after = alternate.extract(id);
            if (!after.empty())
                accepted.insert(std::move(after));
            if (!before.empty())
                alternate.insert(std::move(before));
        }
    }

    domain::Graph& graph_;
    std::string label_;
    domain::Graph alternate_;
    std::vector<domain::GraphId> nodes_, connections_;
    domain::GraphId& activePatch_;
    domain::GraphId patch_;
};
}

Result WorkspaceService::createNode(domain::Position position, std::string name, std::map<std::string, domain::Parameter> parameters)
{
    if (!validPosition(position))
        return Result::failure("Node position must be finite and within +/-1,000,000 patch units.");
    if (name.empty() || name.size() > 128
        || std::all_of(name.begin(), name.end(), [](unsigned char ch) { return ch <= 32; })
        || std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }))
        return Result::failure("Use a non-blank node label of 1-128 bytes without control characters.");
    if (nextId_ == std::numeric_limits<domain::GraphId>::max())
        return Result::failure("The workspace has exhausted its stable object identities.");
    for (const auto& [key, value] : parameters)
        if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos
            || (std::holds_alternative<std::string>(value) && std::get<std::string>(value).find('\0') != std::string::npos)
            || (std::holds_alternative<double>(value) && !std::isfinite(std::get<double>(value))))
            return Result::failure("Initial node parameters need valid keys and finite scalar values.");
    const auto id = nextId_;
    domain::Graph alternate;
    alternate.nodes.emplace(id, domain::Node{id, std::move(name), position, std::move(parameters)});
    auto result = history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Create node", std::move(alternate), std::vector{id}, std::vector<domain::GraphId>{}, state_.activePatch));
    if (result.succeeded)
    {
        ++nextId_;
        lastCreatedNode_ = id;
    }
    return result;
}

Result WorkspaceService::deleteNodes(const std::vector<domain::GraphId>& nodes)
{
    const std::set<domain::GraphId> unique(nodes.begin(), nodes.end());
    if (unique.empty())
        return Result::failure("Select at least one node to delete.");
    for (auto id : unique)
        if (!graph().nodes.contains(id))
            return Result::failure("Cannot delete a node that is no longer in the graph.");
    std::vector<domain::GraphId> connections;
    for (const auto& [id, wire] : graph().connections)
        if (unique.contains(wire.source.node) || unique.contains(wire.destination.node))
            connections.push_back(id);
    return history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Delete nodes", domain::Graph{},
        std::vector<domain::GraphId>(unique.begin(), unique.end()), std::move(connections), state_.activePatch));
}

Result WorkspaceService::moveNodes(const std::map<domain::GraphId, domain::Position>& positions)
{
    if (positions.empty())
        return Result::failure("Select at least one node to move.");
    domain::Graph alternate;
    std::vector<domain::GraphId> nodes;
    for (const auto& [id, position] : positions)
    {
        const auto found = graph().nodes.find(id);
        if (found == graph().nodes.end() || !validPosition(position))
            return Result::failure("Cannot move missing nodes or use invalid patch coordinates.");
        if (position == found->second.position)
            continue;
        auto node = found->second;
        node.position = position;
        alternate.nodes.emplace(id, std::move(node));
        nodes.push_back(id);
    }
    if (nodes.empty())
        return Result::success();
    return history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Move nodes", std::move(alternate), std::move(nodes), std::vector<domain::GraphId>{}, state_.activePatch));
}

Result WorkspaceService::canConnect(domain::Endpoint source, domain::Endpoint destination) const
{
    if (source.port != domain::Port::output || destination.port != domain::Port::input)
        return Result::failure("Connect an output port to an input port.");
    if (!graph().nodes.contains(source.node) || !graph().nodes.contains(destination.node))
        return Result::failure("Both connection endpoints must belong to this graph.");
    if (source.node == destination.node)
        return Result::failure("A node cannot connect to itself.");

    std::unordered_map<domain::GraphId, std::vector<domain::GraphId>> outgoing;
    outgoing.reserve(graph().nodes.size());
    for (const auto& [id, wire] : graph().connections)
    {
        (void) id;
        if (wire.source == source && wire.destination == destination)
            return Result::failure("These ports are already connected.");
        if (wire.destination == destination)
            return Result::failure("This input already has a connection. Disconnect it first.");
        outgoing[wire.source.node].push_back(wire.destination.node);
    }
    std::vector<domain::GraphId> pending{destination.node};
    std::unordered_set<domain::GraphId> visited;
    while (!pending.empty())
    {
        const auto node = pending.back();
        pending.pop_back();
        if (node == source.node)
            return Result::failure("This connection would create a cycle. Feedback is not supported.");
        if (!visited.insert(node).second)
            continue;
        if (const auto found = outgoing.find(node); found != outgoing.end())
            pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    return Result::success();
}

Result WorkspaceService::connect(domain::Endpoint source, domain::Endpoint destination)
{
    if (auto result = canConnect(source, destination); !result.succeeded)
        return result;
    if (nextId_ == std::numeric_limits<domain::GraphId>::max())
        return Result::failure("The workspace has exhausted its stable object identities.");
    const auto id = nextId_;
    domain::Graph alternate;
    alternate.connections.emplace(id, domain::Connection{id, source, destination});
    auto result = history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Connect ports", std::move(alternate), std::vector<domain::GraphId>{}, std::vector{id}, state_.activePatch));
    if (result.succeeded)
        ++nextId_;
    return result;
}

Result WorkspaceService::disconnect(domain::GraphId connection)
{
    if (!graph().connections.contains(connection))
        return Result::failure("Select an existing connection to disconnect.");
    return history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Disconnect ports", domain::Graph{}, std::vector<domain::GraphId>{},
        std::vector{connection}, state_.activePatch));
}

Result WorkspaceService::setParameter(domain::GraphId node, std::string key, domain::Parameter value)
{
    if (!graph().nodes.contains(node) || key.empty() || key.size() > 128 || key.find('\0') != std::string::npos
        || (std::holds_alternative<std::string>(value) && std::get<std::string>(value).find('\0') != std::string::npos)
        || (std::holds_alternative<double>(value) && !std::isfinite(std::get<double>(value))))
        return Result::failure("Parameter needs an existing node, a 1-128 byte key and a finite scalar value.");
    auto replacement = graph().nodes.at(node);
    replacement.parameters.insert_or_assign(std::move(key), std::move(value));
    if (replacement == graph().nodes.at(node))
        return Result::success();
    domain::Graph alternate;
    alternate.nodes.emplace(node, std::move(replacement));
    return history_.execute(std::make_unique<GraphEdit>(
        activeGraph(), "Set parameter", std::move(alternate), std::vector{node}, std::vector<domain::GraphId>{}, state_.activePatch));
}
}
