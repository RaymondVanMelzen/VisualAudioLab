#pragma once

#include "VisualizationWidgets.h"
#include <val/application/WorkspaceService.h>
#include <val/application/Settings.h>
#include <val/audio/AudioEngine.h>
#include <map>

namespace val::presentation
{
/**
 * Message-thread runtime composition and non-modal device/transport controls.
 * Backend creation is injected by Desktop; discovery/probes are automatic, Start is explicit.
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
    /** Restore hints and revalidate automatically; never start playback on launch. */
    void restoreSetup(std::optional<application::AudioSetupPreference> preference);
    [[nodiscard]] std::optional<application::AudioSetupPreference> setupPreference() const;
    [[nodiscard]] std::vector<std::string> endpointChannelNames(domain::EndpointRole role) const;
    [[nodiscard]] std::vector<int> selectedEndpointChannelIndices(domain::GraphId endpoint) const;
    [[nodiscard]] application::Result assignEndpointChannels(domain::GraphId endpoint,
                                                               const std::vector<int>& channelIndices);
    [[nodiscard]] juce::String endpointReadiness(domain::GraphId endpoint) const;
    [[nodiscard]] juce::String compactStatus() const;
    void parameter(std::uint64_t node, std::uint16_t index, float value, audio::ParameterAction action);
    void schedule(std::uint64_t node, std::uint16_t index, float value, double seconds);
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool ready() const { return !running_ && request().value.has_value(); }
    [[nodiscard]] int channels() const noexcept;
    [[nodiscard]] juce::String status() const { return lastStatus_; }
    [[nodiscard]] audio::AudioEngine& engine() noexcept { return engine_; }
    std::function<void(juce::String)> onStatus;
    std::function<void()> onStarted;
    std::function<void()> onDismissSetup;
private:
    struct ResolvedAudioSetup
    {
        application::AudioStreamRequest stream;
        std::map<std::uint64_t, std::vector<int>> endpointChannelMaps;
    };
    void timerCallback() override;
    void selectBackend();
    void scan();
    void inventory(const application::AudioBackendSnapshot& snapshot);
    void inspect();
    void capabilities(application::AudioQuery<application::AudioDeviceCapabilities> result);
    void selectionChanged(bool outputChanged = false);
    void clearConfiguration(juce::String placeholder);
    void populateConfiguration();
    void configurationChanged();
    void controlPanel();
    void message(juce::String text);
    void enableControls();
    [[nodiscard]] application::AudioQuery<audio::AudioGraph> compileGraph(
        bool captures, const std::map<std::uint64_t, std::vector<int>>& endpointChannelMaps = {});
    [[nodiscard]] application::AudioQuery<ResolvedAudioSetup> request() const;
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
    bool running_{}, offline_{}, probing_{}, scanning_{};
    std::optional<application::AudioSetupPreference> preference_;
    std::optional<application::AudioSetupPreference> endpointHint_;
    double rate_{48000};
    int block_{512}, width_{2};
    std::array<float, 4096 * 32> offlineOutput_{};
    std::array<float*, 32> offlinePointers_{};
    juce::ComboBox api_, input_, output_, rateChoice_, buffer_;
    juce::TextButton scan_{"Refresh / Retry"}, panel_{"Control Panel"}, start_{"Start"}, stop_{"Stop"}, done_{"Done"};
    std::array<juce::Label, 5> labels_;
    juce::Label notice_, state_;
    juce::String lastStatus_{"Stopped. Discovering devices; choose endpoints and ports. Start is explicit."};
    juce::String graphWarning_;
    int ticks_{};
};
}
