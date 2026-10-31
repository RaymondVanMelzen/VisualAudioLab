#pragma once

#include "VisualizationWidgets.h"
#include <val/application/WorkspaceService.h>
#include <val/audio/AudioEngine.h>
#include <map>

namespace val::presentation
{
/**
 * Message-thread runtime composition and non-modal device/transport controls.
 * Backend creation is injected by Desktop; discovery/probes/start are explicit user actions.
 * Owns engine before backend shutdown, preserves accepted audio on invalid edits, and retains
 * captures until producers quiesce. Offline preview drives the same engine without any device.
 */
class RuntimePanel final : public juce::Component, private juce::Timer
{
public:
    using BackendFactory = std::function<std::unique_ptr<application::IAudioBackend>(application::AudioBackendKind)>;
    RuntimePanel(application::WorkspaceService& workspace, VisualizationSession& visualizations, BackendFactory factory = {});
    ~RuntimePanel() override;
    void resized() override;
    /** Called after document/history edits; patch replacement first requires stop(). */
    void synchronize();
    [[nodiscard]] application::Result start();
    [[nodiscard]] application::Result stop();
    /** Explicit offline workflow/test seam; never enumerates or opens hardware. */
    void configureOffline(int channels, double rate = 48000, int frames = 512);
    void parameter(std::uint64_t node, std::uint16_t index, float value, audio::ParameterAction action);
    void schedule(std::uint64_t node, std::uint16_t index, float value, double seconds);
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] int channels() const noexcept { return outputCount_.getSelectedId() > 0 ? outputCount_.getSelectedId() : 2; }
    [[nodiscard]] juce::String status() const { return lastStatus_; }
    [[nodiscard]] audio::AudioEngine& engine() noexcept { return engine_; }
    std::function<void(juce::String)> onStatus;
    std::function<void()> onStarted;
private:
    void timerCallback() override;
    void selectBackend();
    void scan();
    void inventory(const application::AudioBackendSnapshot& snapshot);
    void inspect();
    void capabilities(bool input, application::AudioQuery<application::AudioDeviceCapabilities> result);
    void populateConfiguration();
    void message(juce::String text);
    void enableControls();
    [[nodiscard]] application::AudioQuery<audio::AudioGraph> compileGraph(bool captures);
    [[nodiscard]] application::AudioQuery<application::AudioStreamRequest> request() const;
    [[nodiscard]] bool parameterOnly(const domain::Graph& next) const;
    [[nodiscard]] application::Result attachCaptures(audio::AudioGraph& graph, std::vector<std::uint64_t>& added);
    [[nodiscard]] application::Result applyParameters(const domain::Graph& next);
    application::WorkspaceService& workspace_;
    VisualizationSession& visualizations_;
    BackendFactory factory_;
    std::unique_ptr<application::IAudioBackend> backend_;
    audio::AudioEngine engine_;
    std::vector<application::AudioDeviceInfo> inputs_, outputs_;
    std::optional<application::AudioDeviceCapabilities> inputCaps_, outputCaps_;
    std::uint64_t probeSerial_{}, revision_{};
    domain::Graph accepted_;
    domain::GraphId patch_{};
    audio::AudioGraph acceptedAudio_;
    std::map<std::uint64_t, std::shared_ptr<audio::SignalCaptureBuffer>> captures_;
    bool running_{}, offline_{}, probing_{};
    double rate_{48000};
    int block_{512}, width_{2};
    std::array<float, 4096 * 32> offlineOutput_{};
    std::array<float*, 32> offlinePointers_{};
    juce::ComboBox api_, input_, output_, rateChoice_, buffer_, inputCount_, outputCount_;
    juce::TextButton scan_{"Scan devices"}, inspect_{"Inspect selected devices"}, start_{"Start"}, stop_{"Stop"};
    std::array<juce::Label, 7> labels_;
    juce::Label notice_, state_;
    juce::String lastStatus_{"Stopped. Choose an API, scan, select devices, then inspect capabilities. No device opens automatically."};
    juce::String graphWarning_;
    int ticks_{};
};
}
