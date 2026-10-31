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
 * returning. Implementations must not throw or retain a queried device handle.
 */
class IAudioDeviceDiscovery
{
public:
    virtual ~IAudioDeviceDiscovery() = default;
    virtual void setChanged(std::function<void()> changed) = 0;
    [[nodiscard]] virtual application::AudioQuery<AudioDiscoveryInventory> scan() = 0;
    [[nodiscard]] virtual application::AudioQuery<application::AudioDeviceCapabilities>
        probe(const DiscoveredAudioDevice& device) = 0;
    virtual void stop() = 0;
};
}
