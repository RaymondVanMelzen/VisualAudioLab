#pragma once

#include <cstdint>
#include <optional>

namespace val::application
{
/** Device-independent planar PCM span borrowed for one serial callback; null inputs mean silence. */
struct AudioBuffer
{
    const float* const* inputs{};
    float* const* outputs{};
    int inputChannels{}, outputChannels{}, frames{};
};

/** Fully negotiated device configuration and predeclared callback capacity. */
struct AudioStreamFormat
{
    double sampleRate{};
    int bufferFrames{}, maxFrames{}, inputChannels{}, outputChannels{};
    bool operator==(const AudioStreamFormat&) const = default;
};

/** Callback fault categories; no string allocation or notification is performed on the audio thread. */
enum class AudioStreamStop { requested, deviceLost, deviceError, formatChanged };

/**
 * Borrowed render target. process() is serial, non-throwing and real-time safe.
 * interrupted() may occur on a native thread and may only perform lock-free signaling.
 * The owner must retain the target until stopStream() has established callback quiescence.
 */
class IAudioRenderCallback
{
public:
    virtual ~IAudioRenderCallback() = default;
    virtual void process(const AudioBuffer& buffer) noexcept = 0;
    virtual void interrupted(AudioStreamStop reason) noexcept = 0;
};

/** Backend-reported telemetry, not measured physical roundtrip or whole-process CPU. */
struct AudioStreamDiagnostics
{
    bool open{}, running{};
    std::optional<int> inputLatencyFrames, outputLatencyFrames;
    std::optional<std::uint64_t> backendXruns;
};
}
