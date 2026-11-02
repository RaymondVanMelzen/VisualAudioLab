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
/** Fixed workspace panel with an accessible in-place collapse action. */
class WorkspacePanel final : public juce::Component
{
public:
    WorkspacePanel(PanelId panel, std::function<void()> collapseRequested, juce::Component* content = nullptr);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setContent(juce::Component& content);
    [[nodiscard]] PanelId panelId() const noexcept { return panel_; }

private:
    PanelId panel_;
    juce::Label heading_;
    juce::TextButton collapse_{"-"};
    juce::Component* content_{};
};

/** Fixed left-browser / patch-canvas / right-inspector workspace. */
class DockWorkspace final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit DockWorkspace(application::WorkspaceService& workspace);
    ~DockWorkspace() override;
    void resized() override;
    void paint(juce::Graphics& graphics) override;
    void showPanel(PanelId panel);
    void setPanelVisible(PanelId panel, bool visible);
    void resetLayout();
    [[nodiscard]] const DockLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] WorkspacePanel& panel(PanelId id) noexcept;
    [[nodiscard]] CanvasView& canvas() noexcept { return canvas_; }
    [[nodiscard]] VisualizationSession& visualizations() noexcept { return visualizations_; }
    [[nodiscard]] VisualizationWidgets& signalPreview() noexcept { return signalPreview_; }
    void showSignalInspector();
    void setPanelContent(PanelId panel, juce::Component& content) { this->panel(panel).setContent(content); }
    [[nodiscard]] application::ProjectLayout projectLayout() const;
    void restoreLayout(const application::ProjectLayout& value);
    std::function<void()> onLayoutChanged;

private:
    class Divider final : public juce::Component
    {
    public:
        explicit Divider(std::function<void(int)> moved);
        void paint(juce::Graphics& graphics) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        bool keyPressed(const juce::KeyPress& key) override;
    private:
        int previous_{};
        std::function<void(int)> moved_;
    };

    class SignalInspectorWindow final : public juce::DocumentWindow
    {
    public:
        explicit SignalInspectorWindow(VisualizationSession& session);
        void closeButtonPressed() override { setVisible(false); }
        [[nodiscard]] VisualizationWidgets& view() { return *static_cast<VisualizationWidgets*>(getContentComponent()); }
    };

    void changed();
    void showSignalInspectorWindow();
    void resizedPanelContent();

    application::WorkspaceService& workspace_;
    CanvasView canvas_;
    VisualizationSession visualizations_;
    VisualizationWidgets signalPreview_{visualizations_};
    std::array<std::unique_ptr<WorkspacePanel>, DockLayout::panelCount> panels_;
    juce::TextButton reopenBrowser_{">"}, reopenInspector_{"<"};
    Divider leftDivider_, rightDivider_;
    DockLayout layout_;
    float leftRatio_{0.23f}, rightRatio_{0.25f};
    std::unique_ptr<SignalInspectorWindow> signalWindow_;
};
}
