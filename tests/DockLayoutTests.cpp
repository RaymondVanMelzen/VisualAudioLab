#include "TestSupport.h"
#include <val/presentation/DockLayout.h>

int main()
{
    return runTests([]
    {
        using namespace val::presentation;
        DockLayout layout;
        const DockLayout defaults;
        require(layout.placement(PanelId::patchCanvas).position == DockPosition::centre,
                "The canvas starts in the centre.");
        require(layout.placement(PanelId::moduleBrowser).position == DockPosition::left
                && layout.placement(PanelId::projectExplorer).position == DockPosition::left,
                "Discovery panels share the left dock.");
        require(layout.placement(PanelId::propertyInspector).position == DockPosition::right
                && layout.placement(PanelId::audioSettings).position == DockPosition::bottom,
                "Properties and audio settings have dedicated default regions.");

        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            require(layout.placement(id).visible, "Every named area starts available.");
            for (const auto position : {DockPosition::left, DockPosition::centre,
                                       DockPosition::right, DockPosition::bottom})
            {
                layout.move(id, position);
                require(layout.placement(id).position == position && layout.placement(id).visible,
                        "Every panel can move into every fixed dock.");
            }
            layout.setVisible(id, false);
            require(layout.placement(id).visible == (id == PanelId::patchCanvas),
                    "Only the canvas is protected from closure.");
            layout.move(id, DockPosition::left);
            require(layout.placement(id).visible, "Moving a hidden panel reopens it.");
            layout.setVisible(id, true);
            require(layout.placement(id).visible, "A panel can be explicitly reopened.");
        }

        layout.reset();
        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            require(layout.placement(id) == defaults.placement(id),
                    "Reset restores every destination and visibility flag.");
        }
    });
}
