#include <val/application/WorkspaceService.h>
#include <val/domain/Routing.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>

namespace val::application
{
namespace
{
bool label(const std::string& value)
{
    return !value.empty() && value.size() <= 128
        && !std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch <= 32; })
        && std::none_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
}

/** Retains an extracted patch on undo so prior/redo graph command targets stay alive. */
class AddPatch final : public Command
{
public:
    AddPatch(ProjectState& state, domain::GraphId id, domain::GraphId firstEndpoint, std::string name)
        : state_(state), id_(id), previous_(state.activePatch)
    {
        Patch patch{id, std::move(name), {}, {}};
        patch.graph.endpoints.emplace(firstEndpoint, domain::HardwareEndpoint{firstEndpoint,
            domain::EndpointRole::hardwareInput, "Hardware Input 1", true, 2, {}});
        patch.graph.endpoints.emplace(firstEndpoint + 1, domain::HardwareEndpoint{firstEndpoint + 1,
            domain::EndpointRole::hardwareOutput, "Main Output", true, 2, {}});
        prepared_.emplace(id, std::move(patch));
    }
    std::string_view name() const noexcept override { return "Add patch"; }
    Result execute() override
    {
        state_.patches.insert(prepared_.extract(id_));
        state_.activePatch = id_;
        return Result::success();
    }
    Result undo() override
    {
        prepared_.insert(state_.patches.extract(id_));
        state_.activePatch = previous_;
        return Result::success();
    }
private:
    ProjectState& state_;
    domain::GraphId id_, previous_;
    std::map<domain::GraphId, Patch> prepared_;
};
}

Result validateProject(const ProjectState& state)
{
    if (!label(state.workspace.name) || state.patches.empty() || !state.patches.contains(state.activePatch))
        return Result::failure("Project needs a valid name, at least one patch and an existing active patch.");
    const auto& layout = state.layout;
    if (layout.width < 800 || layout.height < 600 || layout.width > 7680 || layout.height > 4320
        || std::abs(static_cast<std::int64_t>(layout.x)) > 1000000
        || std::abs(static_cast<std::int64_t>(layout.y)) > 1000000 || !layout.visible[0])
        return Result::failure("Project window bounds are invalid or the Patch Canvas is hidden.");
    for (auto dock : layout.docks)
        if (dock < 0 || dock > 3)
            return Result::failure("Project contains an unsupported docking destination.");
    for (auto ratio : layout.ratios)
        if (!std::isfinite(ratio) || ratio < 0.1 || ratio > 0.5)
            return Result::failure("Dock proportions must be finite values between 0.1 and 0.5.");
    for (auto selected : layout.selectedPanels)
        if (selected < -1 || selected > 4)
            return Result::failure("Selected panel identity is invalid.");
    std::set<domain::GraphId> identities;
    const auto validId = [&](domain::GraphId id)
    {
        return id != 0 && id < std::numeric_limits<domain::GraphId>::max() && identities.insert(id).second;
    };
    for (const auto& [id, patch] : state.patches)
    {
        if (id == 0 || id == std::numeric_limits<domain::GraphId>::max() || patch.id != id || !label(patch.name))
            return Result::failure("Patch identity or name is invalid.");
        const auto& viewport = patch.viewport;
        if (!std::isfinite(viewport.zoom) || viewport.zoom < 0.025 || viewport.zoom > 3
            || !std::isfinite(viewport.x) || !std::isfinite(viewport.y)
            || std::abs(viewport.x) > 3000000 || std::abs(viewport.y) > 3000000)
            return Result::failure("Patch viewport is outside the supported zoom/pan range.");
        const auto& graph = patch.graph;
        if (std::none_of(graph.endpoints.begin(), graph.endpoints.end(), [](const auto& endpoint)
            { return endpoint.second.role == domain::EndpointRole::hardwareOutput; }))
            return Result::failure("Each patch must retain at least one hardware output row.");
        std::unordered_map<domain::GraphId, std::size_t> incoming;
        std::unordered_map<domain::GraphId, std::vector<domain::GraphId>> outgoing;
        std::set<domain::GraphId> occupied;
        std::set<std::pair<domain::GraphId, domain::GraphId>> edges;
        for (const auto& [nodeId, node] : graph.nodes)
        {
            if (!validId(nodeId) || node.id != nodeId || !label(node.name)
                || !std::isfinite(node.position.x) || !std::isfinite(node.position.y)
                || std::abs(node.position.x) > 1000000 || std::abs(node.position.y) > 1000000)
                return Result::failure("Node identity, name or position is invalid.");
            for (const auto& [key, value] : node.parameters)
                if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos
                    || (std::holds_alternative<std::string>(value) && std::get<std::string>(value).find('\0') != std::string::npos)
                    || (std::holds_alternative<double>(value) && !std::isfinite(std::get<double>(value))))
                    return Result::failure("Parameter key or scalar value is invalid.");
            incoming.emplace(nodeId, 0);
        }
        for (const auto& [endpointId, endpoint] : graph.endpoints)
        {
            const bool validRole = endpoint.role == domain::EndpointRole::hardwareInput
                || endpoint.role == domain::EndpointRole::hardwareOutput;
            if (!validId(endpointId) || endpoint.id != endpointId || !label(endpoint.name)
                || !validRole
                || endpoint.layoutChannels < 1 || endpoint.layoutChannels > 32
                || (!endpoint.channelNames.empty()
                    && static_cast<int>(endpoint.channelNames.size()) != endpoint.layoutChannels)
                || std::any_of(endpoint.channelNames.begin(), endpoint.channelNames.end(), [](const auto& name)
                    { return name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos; }))
                return Result::failure("Hardware endpoint identity, label, layout or selected channel names are invalid.");
            incoming.emplace(endpointId, 0);
        }
        for (const auto& [wireId, wire] : graph.connections)
        {
            const auto sourceNode = graph.nodes.find(wire.source.node);
            const auto destinationNode = graph.nodes.find(wire.destination.node);
            const auto sourceEndpoint = graph.endpoints.find(wire.source.node);
            const auto destinationEndpoint = graph.endpoints.find(wire.destination.node);
            const bool sourceExists = sourceNode != graph.nodes.end() || sourceEndpoint != graph.endpoints.end();
            const bool destinationExists = destinationNode != graph.nodes.end() || destinationEndpoint != graph.endpoints.end();
            const bool sourceOutput = sourceEndpoint != graph.endpoints.end()
                ? sourceEndpoint->second.role == domain::EndpointRole::hardwareInput
                : sourceNode != graph.nodes.end() && domain::hasOutputPort(sourceNode->second);
            const bool destinationInput = destinationEndpoint != graph.endpoints.end()
                ? destinationEndpoint->second.role == domain::EndpointRole::hardwareOutput
                : destinationNode != graph.nodes.end() && domain::hasInputPort(destinationNode->second);
            const bool destinationSum = destinationNode != graph.nodes.end()
                && domain::isSumPort(destinationNode->second);
            if (!validId(wireId) || wire.id != wireId || wire.source.port != domain::Port::output
                || wire.destination.port != domain::Port::input || wire.source.index != 0 || wire.destination.index != 0
                || wire.source.node == wire.destination.node || !sourceExists || !destinationExists
                || !sourceOutput || !destinationInput
                || !edges.emplace(wire.source.node, wire.destination.node).second
                || (!occupied.insert(wire.destination.node).second
                    && !destinationSum))
                return Result::failure("Connection has invalid/duplicate identities, endpoints or an occupied input.");
            if (wire.adapter != domain::AudioAdapter::none && wire.adapter != domain::AudioAdapter::monoToStereo)
                return Result::failure("Connection uses an unsupported channel adapter.");
            ++incoming.at(wire.destination.node);
            outgoing[wire.source.node].push_back(wire.destination.node);
        }
        std::vector<domain::GraphId> pending;
        for (const auto& [node, degree] : incoming)
            if (degree == 0)
                pending.push_back(node);
        std::size_t visited{};
        while (!pending.empty())
        {
            const auto node = pending.back();
            pending.pop_back();
            ++visited;
            if (const auto found = outgoing.find(node); found != outgoing.end())
                for (auto destination : found->second)
                    if (--incoming.at(destination) == 0)
                        pending.push_back(destination);
        }
        if (visited != incoming.size())
            return Result::failure("Project contains a connection cycle; feedback is not supported.");
    }
    return Result::success();
}

Result WorkspaceService::replaceProject(ProjectState state)
{
    if (const auto result = validateProject(state); !result.succeeded)
        return result;
    domain::GraphId next{1};
    for (const auto& [id, patch] : state.patches)
    {
        (void) id;
        for (const auto& [node, value] : patch.graph.nodes)
        {
            (void) value;
            next = std::max(next, node + 1);
        }
        for (const auto& [wire, value] : patch.graph.connections)
        {
            (void) value;
            next = std::max(next, wire + 1);
        }
        for (const auto& [endpoint, value] : patch.graph.endpoints)
        {
            (void) value;
            next = std::max(next, endpoint + 1);
        }
    }
    history_.clear();
    using std::swap;
    swap(state_, state);
    nextId_ = next;
    nextPatchId_ = state_.patches.rbegin()->first + 1;
    lastCreatedNode_ = lastCreatedEndpoint_ = 0;
    return Result::success();
}

Result WorkspaceService::addPatch(std::string name)
{
    if (!label(name) || nextPatchId_ == std::numeric_limits<domain::GraphId>::max()
        || nextId_ >= std::numeric_limits<domain::GraphId>::max() - 2)
        return Result::failure("Use a non-blank patch name of at most 128 bytes; patch identities must be available.");
    const auto id = nextPatchId_;
    const auto result = history_.execute(std::make_unique<AddPatch>(state_, id, nextId_, std::move(name)));
    if (result.succeeded) { ++nextPatchId_; nextId_ += 2; }
    return result;
}

Result WorkspaceService::selectPatch(domain::GraphId id)
{
    if (!state_.patches.contains(id))
        return Result::failure("The selected patch no longer exists.");
    state_.activePatch = id;
    return Result::success();
}
}
