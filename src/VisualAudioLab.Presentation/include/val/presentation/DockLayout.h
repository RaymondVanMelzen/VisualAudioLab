#pragma once

#include <array>
#include <cstddef>

namespace val::presentation
{
/** Stable identities for the shell's docked tools and canvas. */
enum class PanelId { patchCanvas, moduleBrowser, projectExplorer, propertyInspector, audioSettings };

/** Fixed docking destinations; floating dock panels are not implemented. */
enum class DockPosition { left, centre, right, bottom };

/** One panel's docking state, independent of JUCE and converted at the project boundary. */
struct PanelPlacement final
{
    DockPosition position;
    bool visible{true};
    bool operator==(const PanelPlacement&) const = default;
};

/**
 * Message-thread layout value shared by dock controls and View commands.
 * The canvas cannot be hidden, so an editor destination always remains available.
 * Panel and position arguments must be named enumerators, not unchecked external integers.
 */
class DockLayout final
{
public:
    static constexpr std::size_t panelCount = 5;

    [[nodiscard]] const PanelPlacement& placement(PanelId panel) const noexcept
    {
        return placements_[static_cast<std::size_t>(panel)];
    }

    void move(PanelId panel, DockPosition position) noexcept
    {
        placements_[static_cast<std::size_t>(panel)] = {position, true};
    }

    void setVisible(PanelId panel, bool visible) noexcept
    {
        placements_[static_cast<std::size_t>(panel)].visible = panel == PanelId::patchCanvas || visible;
    }

    void reset() noexcept { *this = DockLayout{}; }

private:
    std::array<PanelPlacement, panelCount> placements_{{
        {DockPosition::centre}, {DockPosition::left}, {DockPosition::left},
        {DockPosition::right}, {DockPosition::bottom, false}
    }};
};

/** User-facing name shared by tabs, menus, accessibility and toolbar commands. */
[[nodiscard]] inline const char* panelName(PanelId panel) noexcept
{
    switch (panel)
    {
        case PanelId::patchCanvas: return "Patch Canvas";
        case PanelId::moduleBrowser: return "Module Browser";
        case PanelId::projectExplorer: return "Project Explorer";
        case PanelId::propertyInspector: return "Property Inspector";
        case PanelId::audioSettings: return "Audio Setup";
    }
    return "Unknown panel";
}
}
