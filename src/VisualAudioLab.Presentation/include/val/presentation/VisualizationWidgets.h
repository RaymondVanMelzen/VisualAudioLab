#pragma once

#include <val/audio/AnalyzerEngine.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace val::presentation
{
class VisualizationWidgets;
class CanvasView;

/** Source lifecycle is explicit; absent/stopped/removed sources never masquerade as measured silence. */
enum class ObservationState { waiting, processing, stopped, removed, unsupported };
/** Host-specific refresh ceilings and geometry; no independent worker or timer per preview. */
enum class VisualizationHost { embedded, inspector, monitor };
/** Four calibrated Version 1 displays, all derived from the same immutable analysis product. */
enum class VisualizationKind { oscilloscope, spectrum, peak, rms };

/**
 * Message-thread observation service: one analysis worker, one 30 Hz UI scheduler.
 * Register captures before publishing a prepared graph. Remove after callback quiescence/detachment.
 * Runtime composition explicitly supplies sources; the structural shell never starts audio itself.
 */
class VisualizationSession final : private juce::Timer
{
public:
    VisualizationSession();
    ~VisualizationSession() override;
    [[nodiscard]] application::Result add(std::shared_ptr<audio::SignalCaptureBuffer> capture,
                                         juce::String label, audio::AnalyzerProfile profile = {});
    void remove(std::uint64_t source);
    /** Project/patch transition: unpin/reset all targets and disable/detach every old capture generation. */
    void reset();
    void setState(std::uint64_t source, ObservationState state);
    /** One shared newest copy per source, independent of view count. Also usable in GUI tests. */
    void refresh();
    /** Borrows the structural canvas; visible scheduled nodes share min/max sparks at 10 Hz. */
    void bindCanvas(CanvasView* canvas);
    [[nodiscard]] audio::AnalyzerDiagnostics diagnostics() const { return engine_.diagnostics(); }
    [[nodiscard]] juce::StringArray labels() const;
    [[nodiscard]] std::vector<std::uint64_t> sourceIds() const;
private:
    friend class VisualizationWidgets;
    /** UI-owned source record; reading a result never takes the worker's registry mutex. */
    struct Source
    {
        std::uint64_t id{};
        std::shared_ptr<audio::SignalCaptureBuffer> capture;
        std::shared_ptr<audio::AnalysisResults> results;
        std::shared_ptr<const audio::SignalAnalysis> latest;
        juce::String label;
        ObservationState state{ObservationState::waiting};
        double received{};
        bool embeddedDemand{};
        juce::String error;
    };
    void timerCallback() override { refresh(); }
    Source* find(std::uint64_t id);
    void attach(VisualizationWidgets& view);
    void detach(VisualizationWidgets& view);
    audio::AnalyzerEngine engine_;
    std::vector<Source> sources_;
    std::vector<VisualizationWidgets*> views_;
    std::unique_ptr<audio::SignalAnalysis> scratch_{std::make_unique<audio::SignalAnalysis>()};
    CanvasView* canvas_{};
    double nextEmbedded_{};
};

/** One calibrated plot, keyboard-labelled refresh control and measured paint cost. */
class VisualizationWidget final : public juce::Component
{
public:
    VisualizationWidget(VisualizationKind kind, VisualizationHost host);
    [[nodiscard]] application::Result setRefreshHz(int hz);
    [[nodiscard]] int refreshHz() const noexcept { return rate_; }
    void update(std::shared_ptr<const audio::SignalAnalysis> result, juce::String state, double now, bool stale);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    [[nodiscard]] double lastPaintMilliseconds() const noexcept { return lastPaintMs_; }
    [[nodiscard]] double maxPaintMilliseconds() const noexcept { return maxPaintMs_; }
private:
    VisualizationKind kind_;
    VisualizationHost host_;
    int rate_{};
    double next_{}, lastPaintMs_{}, maxPaintMs_{};
    std::shared_ptr<const audio::SignalAnalysis> result_;
    juce::String status_;
    bool stale_{};
    juce::ComboBox refresh_;
};

/**
 * Shared inspector/embedded/pinned host. Inspector and embedded use tabs; monitor uses four plots.
 * Pin holds target identity, not transport storage. Freeze owns a bounded UI copy and suspends demand.
 * Target changes and unfreeze clear old values. Session must outlive all registered hosts.
 */
class VisualizationWidgets final : public juce::Component
{
public:
    /** Select a catalog analyzer's initial display without creating a separate analysis pipeline. */
    void showKind(VisualizationKind kind) { tabs_.setCurrentTabIndex(static_cast<int>(kind)); }
    explicit VisualizationWidgets(VisualizationSession& session, VisualizationHost host = VisualizationHost::inspector);
    ~VisualizationWidgets() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void visibilityChanged() override;
    void setTarget(std::uint64_t source);
    void followSelection(std::uint64_t source);
    [[nodiscard]] std::uint64_t target() const noexcept { return source_; }
    void setPinned(bool pinned);
    [[nodiscard]] bool pinned() const noexcept { return pin_.getToggleState(); }
    void setFrozen(bool frozen);
    [[nodiscard]] bool frozen() const noexcept { return freeze_.getToggleState(); }
    [[nodiscard]] bool demanding() const noexcept { return demand_; }
    [[nodiscard]] VisualizationWidget& plot(VisualizationKind kind) { return *plots_[static_cast<std::size_t>(kind)]; }
    std::function<void()> onOpenDetails;
private:
    friend class VisualizationSession;
    void tick(double now);
    void releaseDemand();
    bool visibleInHierarchy() const;
    VisualizationSession& session_;
    VisualizationHost host_;
    std::uint64_t source_{};
    std::uint64_t resumeAfterSequence_{};
    bool demand_{};
    juce::String admissionError_;
    std::array<std::unique_ptr<VisualizationWidget>, 4> plots_;
    std::shared_ptr<const audio::SignalAnalysis> frozenResult_;
    juce::ComboBox target_;
    juce::ToggleButton pin_{"Pin"}, freeze_{"Freeze"};
    juce::TextButton details_{"Signal Inspector"}, reset_{"Reset hold"};
    juce::Label status_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
};
}
