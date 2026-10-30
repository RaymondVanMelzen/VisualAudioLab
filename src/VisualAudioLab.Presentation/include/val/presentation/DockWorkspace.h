#pragma once

#include "DockLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace val::presentation
{
/**
 * Owned placeholder view with a draggable heading and keyboard-accessible docking selector.
 * Controls disclose unavailable capabilities and never instantiate an audio/device backend.
 */
class WorkspacePanel final : public juce::Component
{
public:
    WorkspacePanel(PanelId panel, std::function<void(int)> dockRequested);
    ~WorkspacePanel() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void setDockPosition(DockPosition position);
    [[nodiscard]] PanelId panelId() const noexcept { return panel_; }

private:
    PanelId panel_;
    juce::Label heading_, description_;
    juce::ComboBox destination_;
    juce::Label deviceLabel_, rateLabel_;
    juce::ComboBox device_, sampleRate_;
};

/**
 * Four fixed tab docks with draggable panel headings and bounded resizable dividers.
 * Owns every view independently of its current tab parent. Layout is transient UI state;
 * rebuilding tabs must not destroy panels or mutate application/service history.
 * All calls and callbacks run on the JUCE message thread.
 */
class DockWorkspace final : public juce::Component, public juce::DragAndDropContainer
{
public:
    DockWorkspace();
    ~DockWorkspace() override;
    void resized() override;
    void paint(juce::Graphics& graphics) override;
    void showPanel(PanelId panel);
    void setPanelVisible(PanelId panel, bool visible);
    void movePanel(PanelId panel, DockPosition position);
    void resetLayout();
    [[nodiscard]] const DockLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] WorkspacePanel& panel(PanelId id) noexcept;
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
    [[nodiscard]] DockArea& area(DockPosition position) noexcept;

    DockLayout layout_;
    std::array<std::unique_ptr<WorkspacePanel>, DockLayout::panelCount> panels_;
    DockArea left_, centre_, right_, bottom_;
    Divider leftDivider_, rightDivider_, bottomDivider_;
    float leftRatio_{0.22f}, rightRatio_{0.24f}, bottomRatio_{0.26f};
};
}
