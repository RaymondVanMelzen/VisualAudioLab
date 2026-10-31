#pragma once

#include "CommandHistory.h"
#include "Project.h"
#include <val/domain/Workspace.h>
#include <val/domain/Graph.h>
#include <vector>

namespace val::application
{
/**
 * Workspace use-case boundary for project identity, patches and structural graph editing.
 * Owns its project before history so command targets outlive their entries.
 * Instances have a stable address: moving or copying would invalidate retained command targets.
 * ProjectSession coordinates storage; no audio/DSP behavior or processing capability is implied.
 */
class WorkspaceService final
{
public:
    WorkspaceService() = default;
    WorkspaceService(const WorkspaceService&) = delete;
    WorkspaceService& operator=(const WorkspaceService&) = delete;
    WorkspaceService(WorkspaceService&&) = delete;
    WorkspaceService& operator=(WorkspaceService&&) = delete;

    [[nodiscard]] const domain::Workspace& workspace() const noexcept { return state_.workspace; }
    [[nodiscard]] Result rename(std::string name);
    [[nodiscard]] Result undo() { return history_.undo(); }
    [[nodiscard]] Result redo() { return history_.redo(); }
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    /** Accepted editor content; all mutation goes through this service and its existing history. */
    [[nodiscard]] const domain::Graph& graph() const noexcept { return state_.patches.at(state_.activePatch).graph; }
    [[nodiscard]] Result createNode(domain::Position position, std::string name = "Node",
        std::map<std::string, domain::Parameter> parameters = {});
    [[nodiscard]] Result deleteNodes(const std::vector<domain::GraphId>& nodes);
    [[nodiscard]] Result moveNodes(const std::map<domain::GraphId, domain::Position>& positions);
    [[nodiscard]] Result connect(domain::Endpoint source, domain::Endpoint destination);
    [[nodiscard]] Result disconnect(domain::GraphId connection);
    /** Validates endpoints, direction, duplicate/fan-in rules and cycles without mutation. */
    [[nodiscard]] Result canConnect(domain::Endpoint source, domain::Endpoint destination) const;
    /** Identity of the most recently accepted insertion (not selection or history state). */
    [[nodiscard]] domain::GraphId lastCreatedNode() const noexcept { return lastCreatedNode_; }
    [[nodiscard]] const ProjectState& project() const noexcept { return state_; }
    /** Transactional replacement clears history only after validation/preparation succeeds. */
    [[nodiscard]] Result replaceProject(ProjectState state);
    [[nodiscard]] Result addPatch(std::string name);
    [[nodiscard]] Result selectPatch(domain::GraphId id);
    [[nodiscard]] Result setParameter(domain::GraphId node, std::string key, domain::Parameter value);
    /** Navigation/layout are captured separately from the content-edit journal. */
    void setLayout(const ProjectLayout& layout) { state_.layout = layout; }
    void setViewport(PatchViewport viewport) { state_.patches.at(state_.activePatch).viewport = viewport; }

private:
    domain::Graph& activeGraph() { return state_.patches.at(state_.activePatch).graph; }
    ProjectState state_;
    domain::GraphId nextId_{1}, lastCreatedNode_{}, nextPatchId_{2};
    CommandHistory history_;
};
}
