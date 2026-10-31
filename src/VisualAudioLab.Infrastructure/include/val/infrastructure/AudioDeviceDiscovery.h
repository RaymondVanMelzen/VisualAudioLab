#pragma once

#include <val/application/IAudioBackend.h>
#include <memory>

namespace val::infrastructure
{
/** Adapter-local endpoint locator before session/generation tokens are attached. */
struct DiscoveredAudioDevice
{
    std::string key, name;
    application::AudioDirection direction{};
    bool systemDefault{};
    bool operator==(const DiscoveredAudioDevice&) const = default;
};

/** Successful native inventory, with limitations preserved as diagnostics. */
struct AudioDiscoveryInventory
{
    std::vector<DiscoveredAudioDevice> devices;
    std::vector<std::string> notes;
    bool operator==(const AudioDiscoveryInventory&) const = default;
};

/**
 * Injectable native discovery seam; production uses JUCE, tests use deterministic devices.
 * All calls/lifetime belong to the message thread. The change callback may only signal
 * invalidation, never query hardware. stop() must unregister and quiesce callbacks before
 * returning. Probes must not retain temporary handles; only an explicitly opened
 * stream retains a native handle until quiescent close. Implementations must not throw.
 */
class IAudioDeviceDiscovery
{
public:
    virtual ~IAudioDeviceDiscovery() = default;
    virtual void setChanged(std::function<void()> changed) = 0;
    [[nodiscard]] virtual application::AudioQuery<AudioDiscoveryInventory> scan() = 0;
    [[nodiscard]] virtual application::AudioQuery<application::AudioDeviceCapabilities>
        probe(const DiscoveredAudioDevice& device) = 0;
    /** Explicit native stream seam. Defaults reject streaming, never synthesize success. */
    [[nodiscard]] virtual application::AudioQuery<application::AudioStreamFormat> open(
        const std::optional<DiscoveredAudioDevice>&, const std::optional<DiscoveredAudioDevice>&,
        const application::AudioStreamRequest&)
    { return {{}, {application::AudioErrorCode::unavailable, "This discovery source does not support streaming."}}; }
    [[nodiscard]] virtual application::AudioError start(application::IAudioRenderCallback&)
    { return {application::AudioErrorCode::unavailable, "No native stream is available."}; }
    virtual void stopStream() {}
    virtual void closeStream() { stopStream(); }
    /** Control-thread invalidation; notify the borrowed renderer then quiesce/release the stream. */
    virtual void interruptStream(application::AudioStreamStop) { closeStream(); }
    [[nodiscard]] virtual application::AudioStreamDiagnostics diagnostics() const { return {}; }
    virtual void stop() = 0;
};
}
