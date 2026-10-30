#pragma once

#include "CommandHistory.h"
#include <val/domain/Workspace.h>

namespace val::application
{
/**
 * Minimal workspace use-case boundary demonstrating document-owned commands.
 * Owns its transient identity before its history so command targets outlive their entries.
 * Instances have a stable address: moving or copying would invalidate retained command targets.
 * No project files, patch editing, or processing capability is implied.
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

private:
    domain::Workspace workspace_;
    CommandHistory history_;
};
}
