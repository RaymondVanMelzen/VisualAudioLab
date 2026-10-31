#pragma once

#include <val/infrastructure/AudioDeviceDiscovery.h>

namespace val::infrastructure
{
/** Shared JUCE message-thread scheduling, tokens, cancellation and native discovery adapter. */
class JuceAudioBackend : public application::IAudioBackend
{
public:
    ~JuceAudioBackend() override;
    [[nodiscard]] application::AudioBackendKind kind() const noexcept override;
    [[nodiscard]] application::AudioQuery<application::AudioBackendSnapshot> snapshot() const override;
    [[nodiscard]] application::AudioError setChangeCallback(ChangeCallback callback) override;
    [[nodiscard]] application::AudioError refresh() override;
    [[nodiscard]] application::AudioError queryCapabilities(application::AudioDeviceToken token,
                                                           CapabilityCallback callback) override;
    [[nodiscard]] application::AudioError queryConfigurationCapabilities(
        std::optional<application::AudioDeviceToken> input, application::AudioDeviceToken output,
        CapabilityCallback callback) override;
    [[nodiscard]] application::AudioQuery<application::AudioStreamFormat>
        openStream(const application::AudioStreamRequest& request) override;
    [[nodiscard]] application::AudioError startStream(application::IAudioRenderCallback& target) override;
    [[nodiscard]] application::AudioError stopStream() override;
    [[nodiscard]] application::AudioError closeStream() override;
    [[nodiscard]] application::AudioQuery<application::AudioStreamDiagnostics> streamDiagnostics() const override;
    [[nodiscard]] application::AudioError shutdown() override;

protected:
    /** Null discovery selects the real platform source; non-null injects an isolated test source. */
    JuceAudioBackend(application::AudioBackendKind kind, std::unique_ptr<IAudioDeviceDiscovery> discovery);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** Windows ASIO discovery; disabled builds and missing installed drivers remain explicit. */
class AsioBackend final : public JuceAudioBackend
{
public:
    explicit AsioBackend(std::unique_ptr<IAudioDeviceDiscovery> discovery = {});
};

/** Windows WASAPI exclusive-mode discovery, never substituted with shared mode. */
class WasapiExclusiveBackend final : public JuceAudioBackend
{
public:
    explicit WasapiExclusiveBackend(std::unique_ptr<IAudioDeviceDiscovery> discovery = {});
};

/** Windows WASAPI shared-mode discovery; does not claim exclusive capabilities. */
class WasapiSharedBackend final : public JuceAudioBackend
{
public:
    explicit WasapiSharedBackend(std::unique_ptr<IAudioDeviceDiscovery> discovery = {});
};

/** Native macOS CoreAudio discovery; explicitly unsupported on other platforms. */
class CoreAudioBackend final : public JuceAudioBackend
{
public:
    explicit CoreAudioBackend(std::unique_ptr<IAudioDeviceDiscovery> discovery = {});
};

/** Preferred API only: ASIO on Windows, CoreAudio on macOS, with no automatic fallback/device choice. */
[[nodiscard]] std::unique_ptr<application::IAudioBackend> createPreferredAudioBackend();
}
