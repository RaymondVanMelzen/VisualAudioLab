#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace val::application
{
/** Explicit native APIs; modes are never silently substituted. */
enum class AudioBackendKind { asio, wasapiExclusive, wasapiShared, coreAudio };
/** Separate endpoints or a single driver with potentially both directions. */
enum class AudioDirection { input, output, duplex };
/** Machine-readable control/discovery failures, not render-thread errors. */
enum class AudioErrorCode
{
    none, notScanned, unsupportedPlatform, disabled, unavailable, staleDevice,
    wrongThread, queueFull, stopped, invalidArgument
};

/** Actionable control-plane status; an empty error represents success. */
struct AudioError
{
    AudioErrorCode code{AudioErrorCode::none};
    std::string message;
    [[nodiscard]] bool succeeded() const noexcept { return code == AudioErrorCode::none; }
};

/** Typed control result; failure never carries a success-shaped value. */
template <typename T>
struct AudioQuery
{
    std::optional<T> value;
    AudioError error;
};

/**
 * Opaque discovery identity, valid only within one backend instance/inventory generation.
 * The key is an adapter locator, not a persistent hardware UID or an OS handle.
 */
struct AudioDeviceToken
{
    AudioBackendKind backend{};
    std::uint64_t instance{}, generation{};
    std::string key;
    bool operator==(const AudioDeviceToken&) const = default;
};

/** Enumerated endpoint; duplex does not promise that both channel counts are nonzero. */
struct AudioDeviceInfo
{
    AudioDeviceToken token;
    std::string name;
    AudioDirection direction{};
    bool systemDefault{};
};

/** Read-only inventory, including explicit unsupported/disabled/error states. */
struct AudioBackendSnapshot
{
    AudioBackendKind backend{};
    std::uint64_t generation{};
    AudioError error{AudioErrorCode::notScanned, "Request device enumeration first."};
    std::vector<AudioDeviceInfo> devices;
    std::vector<std::string> notes;
};

/** Actual settings of an opened stream, never inferred from capability lists or defaults. */
struct AudioCurrentSettings
{
    double sampleRate{};
    int bufferFrames{};
};

/**
 * Driver-reported candidates for one endpoint, not an exhaustive Cartesian product.
 * Empty rate/period lists mean unreported/unknown, not universal support.
 * Channel lists describe only the selected endpoint direction.
 * No application stream is negotiated, so negotiated is always absent. Native
 * driver initialization may touch hardware (JUCE ASIO may briefly start/stop it).
 */
struct AudioDeviceCapabilities
{
    std::vector<std::string> inputChannels, outputChannels;
    std::vector<double> sampleRates;
    std::vector<int> bufferFrames;
    std::optional<int> defaultBufferFrames;
    std::optional<AudioCurrentSettings> negotiated;
    std::vector<std::string> notes;
};

/**
 * Platform-neutral device discovery/control boundary; contains no DSP or streaming API.
 *
 * Construct, call and destroy on the designated control thread (JUCE message thread in
 * the native adapter). Refresh is coalesced; capability requests are bounded and
 * asynchronous. Accepted callbacks run later on that same thread, never inside a
 * native notification or real-time callback. Callbacks must be short and non-throwing.
 * Replacing a listener cancels its pending notifications. Shutdown/destruction cancels
 * pending work without callbacks; captured callback resources are released on the owner
 * thread. Reentrant requests are queued, not recursively dispatched. No method is RT-safe.
 */
class IAudioBackend
{
public:
    using ChangeCallback = std::function<void(const AudioBackendSnapshot&)>;
    using CapabilityCallback = std::function<void(AudioQuery<AudioDeviceCapabilities>)>;
    virtual ~IAudioBackend() = default;
    [[nodiscard]] virtual AudioBackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual AudioQuery<AudioBackendSnapshot> snapshot() const = 0;
    [[nodiscard]] virtual AudioError setChangeCallback(ChangeCallback callback) = 0;
    [[nodiscard]] virtual AudioError refresh() = 0;
    /** Rejection returns an error immediately and does not invoke callback. */
    [[nodiscard]] virtual AudioError queryCapabilities(AudioDeviceToken token, CapabilityCallback callback) = 0;
    /** Idempotent owner-thread cancellation; no callbacks occur after return. */
    [[nodiscard]] virtual AudioError shutdown() = 0;
};
}
