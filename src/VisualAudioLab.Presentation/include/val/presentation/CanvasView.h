#pragma once

#include "CanvasScene.h"
#include <val/application/WorkspaceService.h>
#include <functional>
#include <optional>

namespace val::presentation
{
/**
 * Single message-thread component rendering a culled structural graph.
 * The borrowed workspace outlives this view; all accepted content edits use its existing
 * CommandHistory. Selection and viewport are local navigation state. Gestures preview outside
 * the model and commit once on release; Escape/focus loss cancels without a history entry.
 */
class CanvasView final : public juce::Component
{
public:
    explicit CanvasView(application::WorkspaceService& workspace);
    ~CanvasView() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    void focusLost(FocusChangeType) override;

    /** Rebuilds view caches after an external undo/redo or accepted service operation. */
    void synchronise();
    void createNodeAt(juce::Point<float> screenPoint);
    void deleteSelection();
    void selectAll();
    void fitGraph();
    void cancelGesture();
    void zoomAt(float zoom, juce::Point<float> screenPoint);
    void panBy(juce::Point<float> screenDelta);
    [[nodiscard]] juce::Point<float> toPatch(juce::Point<float> screen) const;
    [[nodiscard]] juce::Point<float> toScreen(juce::Point<float> patch) const;
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    [[nodiscard]] const std::set<domain::GraphId>& selection() const noexcept { return selected_; }
    /** Rectangle is in patch units; intersecting node bodies are selected, not cables. */
    void selectRectangle(juce::Rectangle<float> rectangle, bool add, bool toggle);
    [[nodiscard]] domain::GraphId nodeAt(juce::Point<float> screenPoint) const;
    [[nodiscard]] std::optional<domain::Endpoint> portAt(juce::Point<float> screenPoint) const;
    [[nodiscard]] domain::GraphId connectionAt(juce::Point<float> screenPoint) const;
    [[nodiscard]] juce::Rectangle<int> viewport() const;
    [[nodiscard]] application::PatchViewport navigation() const { return {zoom_, offset_.x, offset_.y}; }
    void restoreNavigation(application::PatchViewport value);
    /** A project/patch transition clears object-local focus and selection, not just stale IDs. */
    void resetForProject();
    std::function<void(const application::Result&)> onEdit;

private:
    /** Only one provisional pointer/keyboard operation may own the canvas at a time. */
    enum class Gesture { none, move, marquee, connect, pan, keyboardMove };
    void finish(const application::Result& result);
    void contextMenu(juce::Point<float> screenPoint);
    void updateMove(juce::Point<float> patchDelta);
    void updateMarquee(juce::Point<float> screenPoint, juce::ModifierKeys modifiers);
    void updateDetails();
    [[nodiscard]] domain::Position position(domain::GraphId node) const;
    [[nodiscard]] ConnectionView wireView(const domain::Connection& wire) const;
    [[nodiscard]] juce::Rectangle<float> visiblePatch() const;
    [[nodiscard]] juce::Rectangle<int> dirtyPreview() const;

    application::WorkspaceService& workspace_;
    std::unique_ptr<CanvasScene> scene_;
    std::set<domain::GraphId> selected_, beforeSelection_, movingWires_;
    domain::GraphId focusedNode_{}, selectedWire_{};
    Gesture gesture_{Gesture::none};
    std::map<domain::GraphId, domain::Position> originals_, preview_;
    std::optional<domain::Endpoint> source_;
    juce::Point<float> down_, pointer_, offset_{40, 40}, originalOffset_;
    juce::Rectangle<float> marquee_;
    float zoom_{1.0f};
    bool spaceHeld_{};
    juce::TextButton add_{"Add Node"}, remove_{"Delete"}, fit_{"Fit"}, minus_{"-"}, plus_{"+"};
    juce::Label zoomLabel_, help_;
};
}
