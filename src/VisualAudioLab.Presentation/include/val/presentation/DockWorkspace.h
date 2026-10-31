#pragma once

#include "DockLayout.h"
#include "CanvasView.h"
#include "VisualizationWidgets.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace val::presentation
{
/**
 * Tool panel with a draggable heading and keyboard-accessible docking selector.
 * Borrows host-supplied content without owning it; the host supplies module/editor/runtime controls.
 * The panel container itself never constructs an audio/device backend.
 */
class WorkspacePanel final : public juce::Component
{
public:
    WorkspacePanel(PanelId panel, std::function<void(int)> dockRequested, juce::Component* content = nullptr);
    ~WorkspacePanel() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void setDockPosition(DockPosition position);
    /** Replaces borrowed content without changing ownership or persisted panel identity. */
    void setContent(juce::Component& content);
    [[nodiscard]] PanelId panelId() const noexcept { return panel_; }

private:
    PanelId panel_;
    juce::Label heading_, description_;
    juce::ComboBox destination_;
    juce::Component* content_{};
};

/**
 * Four fixed tab docks with draggable panel headings and bounded resizable dividers.
 * Owns every view and the canvas independently of its current tab parent. Layout is navigation state;
 * rebuilding tabs must not destroy panels or mutate application/service history.
 * All calls and callbacks run on the JUCE message thread.
 */
class DockWorkspace final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit DockWorkspace(application::WorkspaceService& workspace);
    ~DockWorkspace() override;
    void resized() override;
    void paint(juce::Graphics& graphics) override;
    void showPanel(PanelId panel);
    void setPanelVisible(PanelId panel, bool visible);
    void movePanel(PanelId panel, DockPosition position);
    void resetLayout();
    [[nodiscard]] const DockLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] WorkspacePanel& panel(PanelId id) noexcept;
    [[nodiscard]] CanvasView& canvas() noexcept { return canvas_; }
    /** Shared visualization composition boundary; no audio starts from a view. */
    [[nodiscard]] VisualizationSession& visualizations() noexcept { return visualizations_; }
    [[nodiscard]] VisualizationWidgets& signalPreview() noexcept { return signalPreview_; }
    void showSignalInspector();
    void setPanelContent(PanelId panel, juce::Component& content) { this->panel(panel).setContent(content); }
    [[nodiscard]] application::ProjectLayout projectLayout() const;
    void restoreLayout(const application::ProjectLayout& value);
    [[nodiscard]] bool accepts(const juce::DragAndDropTarget::SourceDetails& details) const;
    std::function<void()> onLayoutChanged;

private:
    /** Tabbed docking target accepting only panel headers owned by this workspace. */
    class DockArea final : public juce::TabbedComponent, public juce::DragAndDropTarget
    {
    public:
        DockArea(DockWorkspace& workspace, DockPosition position);
        bool isInterestedInDragSource(const SourceDetails& details) override;
        void itemDropped(const SourceDetails& details) override;
        void paint(juce::Graphics& graphics) override;

    private:
        DockWorkspace& workspace_;
        DockPosition position_;
    };

    /** Mouse and keyboard divider retaining its proportion across window resizes. */
    class Divider final : public juce::Component
    {
    public:
        Divider(bool vertical, std::function<void(int)> moved);
        void paint(juce::Graphics& graphics) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        bool keyPressed(const juce::KeyPress& key) override;

    private:
        bool vertical_;
        int previous_{};
        std::function<void(int)> moved_;
    };

    void rebuild();
    void changed();
    /** Persistent non-modal detail host; closed windows hide and stop demand without losing pin identity. */
    class SignalInspectorWindow final : public juce::DocumentWindow
    {
    public:
        explicit SignalInspectorWindow(VisualizationSession& session);
        void closeButtonPressed() override { setVisible(false); }
        [[nodiscard]] VisualizationWidgets& view() { return *static_cast<VisualizationWidgets*>(getContentComponent()); }
    };
    [[nodiscard]] DockArea& area(DockPosition position) noexcept;

    DockLayout layout_;
    VisualizationSession visualizations_;
    VisualizationWidgets signalPreview_{visualizations_};
    std::unique_ptr<SignalInspectorWindow> signalWindow_;
    CanvasView canvas_;
    std::array<std::unique_ptr<WorkspacePanel>, DockLayout::panelCount> panels_;
    DockArea left_, centre_, right_, bottom_;
    Divider leftDivider_, rightDivider_, bottomDivider_;
    float leftRatio_{0.22f}, rightRatio_{0.24f}, bottomRatio_{0.26f};
};
}
