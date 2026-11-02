#include "TestSupport.h"
#include <val/presentation/DockLayout.h>

int main()
{
    return runTests([]
    {
        using namespace val::presentation;
        DockLayout layout;
        const DockLayout defaults;
        require(layout.placement(PanelId::patchCanvas).visible
                && layout.placement(PanelId::moduleBrowser).visible
                && layout.placement(PanelId::propertyInspector).visible,
                "The fixed canvas and both side panels start visible.");

        layout.setVisible(PanelId::patchCanvas, false);
        require(layout.placement(PanelId::patchCanvas).visible,
                "The patch canvas cannot be collapsed.");
        for (const auto id : {PanelId::moduleBrowser, PanelId::propertyInspector})
        {
            layout.setVisible(id, false);
            require(!layout.placement(id).visible, "A side panel can collapse.");
            layout.setVisible(id, true);
            require(layout.placement(id).visible, "A collapsed side panel can reopen.");
        }

        layout.setVisible(PanelId::moduleBrowser, false);
        layout.reset();
        for (std::size_t index = 0; index < DockLayout::panelCount; ++index)
        {
            const auto id = static_cast<PanelId>(index);
            require(layout.placement(id) == defaults.placement(id),
                    "Reset restores the fixed workspace panel visibility.");
        }
    });
}
