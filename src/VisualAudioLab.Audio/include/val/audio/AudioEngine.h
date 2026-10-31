#pragma once

#include <val/audio/AudioGraph.h>
#include <val/application/IAudioBackend.h>
#include <atomic>
#include <array>
#include <thread>

namespace val::audio
{
/**
 * Fixed-size SPSC handoff; one producer/control and one consumer/audio thread (or inverse).
 * No allocation, retry loops or destruction of owned resources inside push/pop.
 */
template <typename T, std::size_t Capacity>
class AudioSpscQueue final
{
public:
    [[nodiscard]] bool canPush() const noexcept
    { return (write_.load(std::memory_order_relaxed) + 1) % (Capacity + 1) != read_.load(std::memory_order_acquire); }
    [[nodiscard]] bool push(const T& item) noexcept
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = (write + 1) % (Capacity + 1);
        if (next == read_.load(std::memory_order_acquire)) return false;
        data_[write] = item;
        write_.store(next, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool pop(T& item) noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        item = data_[read];
        read_.store((read + 1) % (Capacity + 1), std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool peek(T& item) const noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        item = data_[read];
        return true;
    }
    /** Consumer snapshot bounds one callback's work; later producer writes wait for the next block. */
    [[nodiscard]] std::size_t available() const noexcept
    {
        return (write_.load(std::memory_order_acquire) + Capacity + 1 - read_.load(std::memory_order_relaxed)) % (Capacity + 1);
    }
private:
    std::array<T, Capacity + 1> data_{};
    std::atomic<std::size_t> write_{}, read_{};
};

/** Bounded observation counters; successive fields are independently sampled, not one atomic transaction. */
struct AudioEngineTelemetry
{
    std::uint64_t callbacks{}, frames{}, activeRevision{}, callbackNanoseconds{}, maxCallbackNanoseconds{};
    std::uint64_t deadlineMisses{}, invalidBlocks{}, canceledGainChanges{};
    std::uint64_t epoch{}, samplePosition{}, appliedEvents{}, canceledEvents{}, missedEvents{};
    std::uint64_t lastEventSequence{}, lastEventFrame{};
    std::uint64_t latestPlanNumericalFaults{};
    double averageCallbackLoad{}, lastCallbackLoad{};
    std::optional<double> reportedInputLatencySeconds, reportedOutputLatencySeconds;
    std::optional<std::uint64_t> backendXruns;
    std::optional<double> wholeProcessCpu; // Not collected; absent is not zero.
    bool running{};
    application::AudioStreamStop stopReason{application::AudioStreamStop::requested};
};

/**
 * Serial realtime renderer and control-thread stream owner for the basic graph.
 *
 * Control methods/destruction have one creating-thread owner; backend must outlive
 * the engine until stop() returns. Only one serial render source may call process().
 * startOffline() is an explicit test/external-host path with the same renderer; its
 * caller must quiesce rendering before stop/destruction. No realtime operation owns,
 * allocates, frees, logs or dispatches UI work. Callback atomics are compile-time
 * required to be always lock-free. Control methods are not real-time safe.
 */
class AudioEngine final : public application::IAudioRenderCallback
{
public:
    /** Monotonic nanosecond source; injected clocks must be nonblocking, non-allocating and noexcept. */
    using Clock = std::uint64_t (*)() noexcept;
    explicit AudioEngine(Clock clock = nullptr);
    ~AudioEngine() override;
    [[nodiscard]] application::Result start(application::IAudioBackend& backend,
        const application::AudioStreamRequest& request, const AudioGraph& graph, std::uint64_t revision);
    [[nodiscard]] application::Result startOffline(application::AudioStreamFormat format,
        const AudioGraph& graph, std::uint64_t revision);
    /** Prepare/publish a newer revision; latest pending replaces older pending on the control thread. */
    [[nodiscard]] application::Result publish(const AudioGraph& graph, std::uint64_t revision);
    /** Bounded live event for the latest accepted revision. No implicit persisted-project mutation. */
    [[nodiscard]] application::Result setGain(std::uint64_t revision, std::uint64_t node, float gain);
    /** Unscheduled live transition; eligible at a future callback boundary, not a wall-clock guarantee. */
    [[nodiscard]] application::Result setParameter(std::uint64_t revision, std::uint64_t node,
        std::uint16_t parameter, float value, ParameterAction action = ParameterAction::overrideValue);
    /**
     * Schedule at an absolute session frame. Supply telemetry epoch and nondecreasing frames per revision.
     * At most 128 events/ten seconds ahead may be queued; equal-frame order is acceptance order. Racing/late delivery is
     * explicitly counted as missed, never replayed. Structural replacement cancels old-revision events.
     */
    [[nodiscard]] application::Result scheduleParameter(std::uint64_t epoch, std::uint64_t revision,
        std::uint64_t node, std::uint16_t parameter, std::uint64_t frame, float value,
        ParameterAction action = ParameterAction::automation);
    /** Lock-free fields from the latest accepted plan; a pending/new analyzer initially reports zero windows. */
    [[nodiscard]] application::AudioQuery<AnalyzerSnapshot> analyzerSnapshot(std::uint64_t revision, std::uint64_t node) const;
    /** Owner-thread first-fault observation for the accepted script generation; no callback formatting/logging. */
    [[nodiscard]] application::AudioQuery<script::RuntimeFault> scriptFault(std::uint64_t revision, std::uint64_t node) const;
    /** Frees retired plans only on the owner thread; call regularly while publishing. */
    [[nodiscard]] application::Result collectRetired();
    /** Backend stop/close establishes quiescence before graph reclamation. */
    [[nodiscard]] application::Result stop();
    [[nodiscard]] application::AudioQuery<AudioEngineTelemetry> telemetry() const;
    void process(const application::AudioBuffer& buffer) noexcept override;
    void interrupted(application::AudioStreamStop reason) noexcept override;
private:
    [[nodiscard]] application::Result owner() const;
    void clearPlans();
    void resetTelemetry() noexcept;
    /** Trivial revision-scoped parameter message, never retaining pointers into a plan. */
    struct GainChange
    {
        std::uint64_t revision{}; std::size_t index{}; float value{};
        std::uint16_t parameter{}; ParameterAction action{ParameterAction::overrideValue};
        std::uint64_t sequence{};
    };
    /** Prevalidated scheduled transition; no pointers or owning objects cross the callback boundary. */
    struct ScheduledChange
    {
        GainChange change;
        std::uint64_t frame{};
    };
    std::thread::id owner_;
    Clock clock_;
    application::IAudioBackend* backend_{};
    application::AudioStreamFormat format_{};
    std::vector<float> inputScratch_;
    std::array<const float*, 32> inputPointers_{};
    PreparedAudioGraph* active_{};
    PreparedAudioGraph* latest_{}; // Control-owned lookup only; always the latest non-retired submitted plan.
    std::atomic<PreparedAudioGraph*> pending_{};
    AudioSpscQueue<PreparedAudioGraph*, 8> retired_;
    AudioSpscQueue<GainChange, 32> gains_;
    AudioSpscQueue<ScheduledChange, 128> scheduled_;
    std::uint64_t acceptedRevision_{};
    std::uint64_t epoch_{}, nextSequence_{}, lastScheduledFrame_{};
    bool configured_{};
    std::atomic<bool> enabled_{};
    std::atomic<application::AudioStreamStop> stopReason_{application::AudioStreamStop::requested};
    std::atomic<std::uint64_t> callbacks_{}, frames_{}, revision_{}, lastNs_{}, maxNs_{}, totalNs_{}, lastFrames_{};
    std::atomic<std::uint64_t> misses_{}, invalid_{}, canceled_{};
    std::atomic<std::uint64_t> position_{}, scheduleFloor_{}, appliedEvents_{}, canceledEvents_{}, missedEvents_{};
    std::atomic<std::uint64_t> lastEventSequence_{}, lastEventFrame_{};
};
}
