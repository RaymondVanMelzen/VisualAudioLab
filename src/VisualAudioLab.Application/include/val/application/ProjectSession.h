#pragma once

#include "WorkspaceService.h"

namespace val::application
{
/**
 * Single-control-thread project lifecycle borrowing the workspace and local store.
 * UI obtains discard/save consent before load; failed operations preserve project/path/history.
 * Successful saves retain compatible opaque source data and detect external file changes.
 */
class ProjectSession final
{
public:
    ProjectSession(WorkspaceService& workspace, ProjectStore& store);
    [[nodiscard]] Result load(const std::filesystem::path& path);
    [[nodiscard]] Result save();
    [[nodiscard]] Result saveAs(const std::filesystem::path& path);
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const std::vector<std::filesystem::path>& recent() const noexcept { return recent_; }
    /** Nonfatal recent-list storage errors are visible without misreporting a completed save/load. */
    [[nodiscard]] const std::string& warning() const noexcept { return warning_; }
    [[nodiscard]] Result forgetRecent(const std::filesystem::path& path);

private:
    Result remember(const std::filesystem::path& path);
    WorkspaceService& workspace_;
    ProjectStore& store_;
    ProjectState saved_;
    std::string source_, templateSource_, warning_;
    std::filesystem::path path_;
    std::vector<std::filesystem::path> recent_;
};
}
