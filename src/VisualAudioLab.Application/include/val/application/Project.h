#pragma once

#include "Result.h"
#include <val/domain/Graph.h>
#include <val/domain/Workspace.h>
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace val::application
{
/** Per-patch navigation, saved without selection or undo history. */
struct PatchViewport final
{
    double zoom{1}, x{40}, y{40};
    bool operator==(const PatchViewport&) const = default;
};

/** Named structural patch; identities are stable across save/load and patch switching. */
struct Patch final
{
    domain::GraphId id{1};
    std::string name{"Patch 1"};
    domain::Graph graph;
    PatchViewport viewport;
    bool operator==(const Patch&) const = default;
};

/** Portable shell geometry; panel order matches the five documented shell panel identities. */
struct ProjectLayout final
{
    int x{80}, y{80}, width{1100}, height{720};
    std::array<int, 5> docks{1, 0, 0, 2, 3};
    std::array<bool, 5> visible{true, true, true, true, false};
    std::array<double, 3> ratios{0.22, 0.24, 0.26};
    std::array<int, 4> selectedPanels{1, 0, 3, -1};
    bool operator==(const ProjectLayout&) const = default;
};

/** Complete editable project snapshot, independent of JSON and platform storage. */
struct ProjectState final
{
    domain::Workspace workspace;
    std::map<domain::GraphId, Patch> patches{{1, Patch{}}};
    domain::GraphId activePatch{1};
    ProjectLayout layout;
    bool operator==(const ProjectState&) const = default;
};

/** A validated value plus opaque adapter-owned source text for lossless unknown-field merging. */
struct ProjectFile final
{
    ProjectState state;
    std::string source;
};

/** Failed reads contain no candidate project and never replace the active document. */
struct ProjectReadResult final
{
    std::optional<ProjectFile> file;
    std::string error;
};

/** Local project storage port. Writes update source only after a successful prepared replacement. */
class ProjectStore
{
public:
    virtual ~ProjectStore() = default;
    [[nodiscard]] virtual ProjectReadResult read(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual Result write(const std::filesystem::path& path, ProjectFile& file,
                                      const std::optional<std::string>& expectedSource) = 0;
    [[nodiscard]] virtual Result readRecent(std::vector<std::filesystem::path>& paths) = 0;
    [[nodiscard]] virtual Result writeRecent(const std::vector<std::filesystem::path>& paths) = 0;
};

/** Validates all known entities and graph invariants before accepting a project snapshot. */
[[nodiscard]] Result validateProject(const ProjectState& state);
}
