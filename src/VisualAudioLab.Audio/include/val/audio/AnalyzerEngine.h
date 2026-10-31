#pragma once

#include <val/audio/SignalCaptureBuffer.h>
#include <array>
#include <complex>
#include <mutex>
#include <thread>
#include <vector>

namespace val::audio
{
/** Analysis settings are immutable per admitted source; changing them requires a new capture generation. */
struct AnalyzerProfile
{
    int fftSize{2048};
    double rmsSeconds{0.3}, spectrumAverageSeconds{0.1};
    enum class Trigger { free, rising, falling } trigger{Trigger::free};
    float triggerLevel{};
    int preTriggerFrames{256};
};

/** Calibrated immutable Version 1 results for a shared mono/stereo source. */
struct SignalAnalysis
{
    CaptureStamp stamp;
    bool spectrumValid{}, rmsValid{}, peakValid{}, gap{}, nonFinite{}, triggered{};
    std::uint64_t segment{}, resultVersion{}, captureDrops{}, invalidSamples{};
    std::array<float, 2> peak{}, heldPeak{}, displayedPeak{}, rms{}, mean{}, minimum{}, maximum{};
    std::array<std::uint64_t, 2> overRange{};
    std::array<std::array<float, 512>, 2> scopeMinimum{}, scopeMaximum{};
    int scopeFrames{};
    std::array<std::array<float, 4097>, 2> amplitudeDb{}, densityDb{};
    std::array<double, 2> dominantHz{};
    double binHz{}, coherentGain{}, enbwBins{}, enbwHz{};
    int fftSize{}, rmsFrames{}, coveredFrames{};
    double analysisMilliseconds{};
};

/** Worker/UI diagnostics are off-callback metrics; memory excludes externally owned host/render surfaces. */
struct AnalyzerDiagnostics
{
    std::uint64_t jobs{}, publicationDrops{};
    double lastJobMilliseconds{}, maxJobMilliseconds{};
    std::size_t memoryBytes{}, sources{};
};

/**
 * Three preallocated worker/UI pages with bounded lock-free ownership transitions.
 * The worker may recycle Ready but never Reading. The sole UI consumer takes a copy,
 * releases immediately and shares that copy across widgets; freezing cannot hold a page.
 */
class AnalysisResults final
{
public:
    void publish(const SignalAnalysis& result) noexcept;
    [[nodiscard]] bool latest(SignalAnalysis& result) noexcept;
    [[nodiscard]] std::uint64_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
private:
    enum class Ownership : unsigned { free, writing, ready, reading };
    /** State exclusively protects the adjacent non-atomic payload from concurrent access. */
    struct Page
    {
        std::atomic<Ownership> state{Ownership::free};
        SignalAnalysis payload;
    };
    std::array<Page, 3> pages_;
    std::size_t next_{};
    std::uint64_t lastSequence_{};
    std::atomic<std::uint64_t> drops_{};
};

/**
 * Single shared analysis worker, at most eight sources/sixteen captured channels/64 MiB.
 * Capture registration/destruction is control-only; worker never locks audio-owned state.
 * Results use a three-slot SPSC pool. UI poll releases all slots before returning bounded copies.
 * Manual mode processes one fair round explicitly for deterministic/offline tests.
 */
class AnalyzerEngine final
{
public:
    explicit AnalyzerEngine(bool startWorker = true);
    ~AnalyzerEngine();
    [[nodiscard]] application::Result add(std::shared_ptr<SignalCaptureBuffer> capture, AnalyzerProfile profile = {});
    /** Remove only after detaching/quiescing its audio producer; waits for analysis ownership, never audio. */
    void remove(std::uint64_t source);
    /** Shared UI demand. Hidden/frozen/offscreen hosts must stop demanding capture; other hosts can continue. */
    [[nodiscard]] application::Result demand(std::uint64_t source, bool active);
    /** UI-only newest-result copy; fan-out this result to all views of the same source. */
    [[nodiscard]] bool latest(std::uint64_t source, SignalAnalysis& result);
    /** Control/UI registration lookup only; retain handle and call its lock-free latest in timer ticks. */
    [[nodiscard]] std::shared_ptr<AnalysisResults> results(std::uint64_t source);
    [[nodiscard]] AnalyzerDiagnostics diagnostics() const;
    /** Manual mode only; returns jobs processed, bounded to one packet per source per round. */
    std::size_t pump();
    [[nodiscard]] static application::Result validate(const AnalyzerProfile& profile, double sampleRate);
    [[nodiscard]] static float decibels(float amplitude) noexcept;
    /** Worker-only display hold reset; affects measurements, never processor/stream state. */
    [[nodiscard]] application::Result resetHold(std::uint64_t source);
private:
    /** One source's worker-owned contiguous state, FFT scratch and fixed result ownership pool. */
    struct State;
    std::size_t round();
    void run(std::stop_token stop);
    bool threaded_;
    mutable std::mutex registry_;
    std::vector<std::unique_ptr<State>> sources_;
    std::jthread worker_;
};
}
