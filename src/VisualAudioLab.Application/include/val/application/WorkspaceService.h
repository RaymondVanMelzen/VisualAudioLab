#pragma once

#include "CommandHistory.h"
#include <val/domain/Workspace.h>
#include <val/domain/Graph.h>
#include <vector>

namespace val::application
{
/**
 * Workspace use-case boundary for transient identity and structural graph editing.
 * Owns its identity and graph before history so command targets outlive their entries.
 * Instances have a stable address: moving or copying would invalidate retained command targets.
 * No project files, audio/DSP behavior or processing capability is implied.
 */
class WorkspaceService final
{
public:
    WorkspaceService() = default;
    WorkspaceService(const WorkspaceService&) = delete;
    WorkspaceService& operator=(const WorkspaceService&) = delete;
    WorkspaceService(WorkspaceService&&) = delete;
    WorkspaceService& operator=(WorkspaceService&&) = delete;

    [[nodiscard]] const domain::Workspace& workspace() const noexcept { return workspace_; }
    [[nodiscard]] Result rename(std::string name);
    [[nodiscard]] Result undo() { return history_.undo(); }
    [[nodiscard]] Result redo() { return history_.redo(); }
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    /** Accepted editor content; all mutation goes through this service and its existing history. */
    [[nodiscard]] const domain::Graph& graph() const noexcept { return graph_; }
    [[nodiscard]] Result createNode(domain::Position position, std::string name = "Node");
    [[nodiscard]] Result deleteNodes(const std::vector<domain::GraphId>& nodes);
    [[nodiscard]] Result moveNodes(const std::map<domain::GraphId, domain::Position>& positions);
    [[nodiscard]] Result connect(domain::Endpoint source, domain::Endpoint destination);
    [[nodiscard]] Result disconnect(domain::GraphId connection);
    /** Validates endpoints, direction, duplicate/fan-in rules and cycles without mutation. */
    [[nodiscard]] Result canConnect(domain::Endpoint source, domain::Endpoint destination) const;
    /** Identity of the most recently accepted insertion (not selection or history state). */
    [[nodiscard]] domain::GraphId lastCreatedNode() const noexcept { return lastCreatedNode_; }

private:
    domain::Workspace workspace_;
    domain::Graph graph_;
    domain::GraphId nextId_{1}, lastCreatedNode_{};
    CommandHistory history_;
};
}
