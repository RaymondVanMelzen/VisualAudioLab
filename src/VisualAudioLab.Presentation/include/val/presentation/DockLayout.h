#pragma once

#include <array>
#include <cstddef>

namespace val::presentation
{
enum class PanelId { patchCanvas, moduleBrowser, propertyInspector };

struct PanelPlacement final
{
    bool visible{true};
    bool operator==(const PanelPlacement&) const = default;
};

/** Fixed workspace panel visibility; the canvas is always visible. */
class DockLayout final
{
public:
    static constexpr std::size_t panelCount = 3;

    [[nodiscard]] const PanelPlacement& placement(PanelId panel) const noexcept
    {
        return placements_[static_cast<std::size_t>(panel)];
    }

    void setVisible(PanelId panel, bool visible) noexcept
    {
        placements_[static_cast<std::size_t>(panel)].visible = panel == PanelId::patchCanvas || visible;
    }

    void reset() noexcept { *this = DockLayout{}; }

private:
    std::array<PanelPlacement, panelCount> placements_{};
};

[[nodiscard]] inline const char* panelName(PanelId panel) noexcept
{
    switch (panel)
    {
        case PanelId::patchCanvas: return "Patch Canvas";
        case PanelId::moduleBrowser: return "Module Browser";
        case PanelId::propertyInspector: return "Property Inspector";
    }
    return "Unknown panel";
}
}
