#pragma once

#include "CanvasScene.h"
#include <val/application/WorkspaceService.h>
#include <cmath>
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
class CanvasView final : public juce::Component, public juce::DragAndDropTarget
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
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

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
    [[nodiscard]] juce::Rectangle<int> sourceRailBounds() const;
    [[nodiscard]] juce::Rectangle<int> destinationRailBounds() const;
    [[nodiscard]] std::optional<juce::Point<float>> endpointPortPosition(domain::GraphId, domain::Port) const;
    /** Persist only the precision retained by the float-based canvas transform. */
    [[nodiscard]] application::PatchViewport navigation() const
    {
        const auto stableZoom = [](float value)
        {
            return std::round(static_cast<double>(value) * 1000000.0) / 1000000.0;
        };
        return {stableZoom(zoom_), std::round(static_cast<double>(offset_.x)),
                std::round(static_cast<double>(offset_.y))};
    }
    void restoreNavigation(application::PatchViewport value);
    /** A project/patch transition clears object-local focus and selection, not just stale IDs. */
    void resetForProject();
    std::function<void(const application::Result&)> onEdit;
    std::function<void(std::string, juce::Point<float>)> onModuleDropped;
    std::function<void(juce::String)> onModuleDropRejected;
    std::function<std::vector<std::string>(domain::EndpointRole)> endpointChannels;
    std::function<std::vector<int>(domain::GraphId)> selectedEndpointChannels;
    std::function<juce::String(domain::GraphId)> endpointStatus;
    std::function<application::Result(domain::GraphId, const std::vector<int>&)> assignEndpointChannels;
    /** Single selected node, connected source endpoint, or zero for absent/ambiguous targets. */
    std::function<void(domain::GraphId)> onObservationTarget;
    /** Optional shared observation rendering; never owns a processor, timer or analysis worker. */
    std::function<void(juce::Graphics&, domain::GraphId, juce::Rectangle<float>)> paintObservation;
    [[nodiscard]] bool observationVisible(domain::GraphId node) const;
    void repaintObservation(domain::GraphId node);

private:
    /** Only one provisional pointer/keyboard operation may own the canvas at a time. */
    enum class Gesture { none, move, marquee, connect, pan, keyboardMove };
    void finish(const application::Result& result);
    void contextMenu(juce::Point<float> screenPoint);
    void endpointMenu(domain::GraphId endpoint);
    void updateMove(juce::Point<float> patchDelta);
    void updateMarquee(juce::Point<float> screenPoint, juce::ModifierKeys modifiers);
    void updateDetails();
    void paintEndpointRails(juce::Graphics&);
    void clampRailScroll();
    [[nodiscard]] domain::Position position(domain::GraphId node) const;
    [[nodiscard]] ConnectionView wireView(const domain::Connection& wire) const;
    [[nodiscard]] juce::Rectangle<float> visiblePatch() const;
    [[nodiscard]] juce::Rectangle<int> dirtyPreview() const;
    [[nodiscard]] juce::Rectangle<int> contentBounds() const;
    [[nodiscard]] juce::Rectangle<int> endpointCardBounds(domain::GraphId) const;
    [[nodiscard]] std::vector<domain::GraphId> sourceEndpointIds() const;
    [[nodiscard]] std::vector<domain::GraphId> destinationEndpointIds() const;
    [[nodiscard]] bool fixedEndpoint(domain::GraphId) const;
    [[nodiscard]] float railWidth() const noexcept;

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
    int sourceScroll_{}, destinationScroll_{};
    bool spaceHeld_{};
    juce::TextButton add_{"Routing Draft"}, addInput_{"+ Input"}, addOutput_{"+ Output"};
    juce::TextButton remove_{"Delete"}, fit_{"Fit"}, minus_{"-"}, plus_{"+"};
    juce::Label zoomLabel_, help_;
};
}
