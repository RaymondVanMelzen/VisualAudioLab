#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <new>

namespace val::audio
{
using namespace application;
static_assert(std::atomic<PreparedAudioGraph*>::is_always_lock_free);
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<AudioStreamStop>::is_always_lock_free);

namespace
{
std::uint64_t monotonicNanoseconds() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
AudioEngine::AudioEngine(Clock clock)
    : owner_(std::this_thread::get_id()), clock_(clock ? clock : monotonicNanoseconds) {}
AudioEngine::~AudioEngine()
{
    assert(owner_ == std::this_thread::get_id());
    const auto result = stop();
    assert(result.succeeded);
}
Result AudioEngine::owner() const
{
    return owner_ == std::this_thread::get_id() ? Result::success()
        : Result::failure("AudioEngine control/lifetime belongs to its creating thread.");
}
void AudioEngine::resetTelemetry() noexcept
{
    callbacks_ = 0; frames_ = 0; revision_ = 0; lastNs_ = 0; maxNs_ = 0; totalNs_ = 0; lastFrames_ = 0;
    misses_ = 0; invalid_ = 0; canceled_ = 0; stopReason_ = AudioStreamStop::requested;
}
Result AudioEngine::startOffline(AudioStreamFormat format, const AudioGraph& graph, std::uint64_t revision)
{
    if (const auto check = owner(); !check.succeeded) return check;
    if (configured_) return Result::failure("Stop the existing engine session before reconfiguring.");
    auto prepared = PreparedAudioGraph::prepare(graph, format, revision);
    if (!prepared.value) return Result::failure(prepared.error.message);
    inputScratch_.resize(static_cast<std::size_t>(format.maxFrames) * static_cast<std::size_t>(format.inputChannels));
    for (int channel = 0; channel < format.inputChannels; ++channel)
        inputPointers_[channel] = inputScratch_.data() + channel * format.maxFrames;
    format_ = format;
    active_ = prepared.value->release();
    latest_ = active_;
    acceptedRevision_ = revision;
    configured_ = true;
    resetTelemetry();
    revision_.store(revision, std::memory_order_relaxed);
    enabled_.store(true, std::memory_order_release);
    return Result::success();
}
Result AudioEngine::start(IAudioBackend& backend, const AudioStreamRequest& request,
                          const AudioGraph& graph, std::uint64_t revision)
{
    if (const auto check = owner(); !check.succeeded) return check;
    if (configured_) return Result::failure("Stop the existing engine before opening a device.");
    auto opened = backend.openStream(request);
    if (!opened.value) return Result::failure(opened.error.message);
    Result prepared{false, {}};
    try { prepared = startOffline(*opened.value, graph, revision); }
    catch (const std::bad_alloc&)
    {
        prepared = Result::failure("Insufficient memory while preparing the audio graph.");
    }
    if (!prepared.succeeded)
    {
        const auto closed = backend.closeStream();
        return closed.succeeded() ? prepared : Result::failure(prepared.error + " Device cleanup: " + closed.message);
    }
    backend_ = &backend;
    const auto started = backend.startStream(*this);
    if (!started.succeeded())
    {
        const auto stopped = stop();
        return Result::failure(started.message + (stopped.succeeded ? "" : " Cleanup: " + stopped.error));
    }
    return Result::success();
}
Result AudioEngine::publish(const AudioGraph& graph, std::uint64_t revision)
{
    if (const auto check = owner(); !check.succeeded) return check;
    if (!configured_ || !enabled_.load(std::memory_order_acquire))
        return Result::failure("Start a valid engine session before publishing a graph.");
    if (revision <= acceptedRevision_) return Result::failure("Graph revisions must increase within the current session.");
    auto prepared = PreparedAudioGraph::prepare(graph, format_, revision);
    if (!prepared.value) return Result::failure(prepared.error.message);
    const auto collected = collectRetired();
    if (!collected.succeeded) return collected;
    auto* plan = prepared.value->release();
    delete pending_.exchange(plan, std::memory_order_acq_rel); // Returned mailbox ownership never belongs to the callback.
    latest_ = plan;
    acceptedRevision_ = revision;
    return Result::success();
}
Result AudioEngine::setGain(std::uint64_t revision, std::uint64_t node, float gain)
{
    if (const auto check = owner(); !check.succeeded) return check;
    if (!configured_ || !enabled_.load(std::memory_order_acquire) || revision != acceptedRevision_
        || !std::isfinite(gain) || gain < 0 || gain > 4)
        return Result::failure("Gain needs a running session, the latest accepted revision and a finite linear value in [0,4].");
    std::size_t index{};
    if (!latest_->gainIndex(node, index)) return Result::failure("The requested Gain node does not exist in this revision.");
    if (!gains_.push({revision, index, gain})) return Result::failure("Gain queue is full (32); retry after processing.");
    return Result::success();
}
Result AudioEngine::collectRetired()
{
    if (const auto check = owner(); !check.succeeded) return check;
    PreparedAudioGraph* old{};
    while (retired_.pop(old)) delete old;
    return Result::success();
}
void AudioEngine::clearPlans()
{
    delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    delete active_; active_ = nullptr; latest_ = nullptr;
    const auto collected = collectRetired();
    assert(collected.succeeded);
    GainChange event;
    while (gains_.pop(event)) {}
}
Result AudioEngine::stop()
{
    if (const auto check = owner(); !check.succeeded) return check;
    enabled_.store(false, std::memory_order_release);
    if (backend_)
    {
        const auto stopped = backend_->stopStream();
        if (!stopped.succeeded() && stopped.code != AudioErrorCode::stopped) return Result::failure(stopped.message);
        const auto closed = backend_->closeStream();
        if (!closed.succeeded() && closed.code != AudioErrorCode::stopped) return Result::failure(closed.message);
        backend_ = nullptr;
    }
    clearPlans();
    configured_ = false;
    return Result::success();
}
void AudioEngine::interrupted(AudioStreamStop reason) noexcept
{
    if (reason != AudioStreamStop::requested)
    {
        auto expected = AudioStreamStop::requested;
        stopReason_.compare_exchange_strong(expected, reason, std::memory_order_relaxed);
    }
    enabled_.store(false, std::memory_order_release);
}
void AudioEngine::process(const AudioBuffer& buffer) noexcept
{
    const auto began = clock_();
    // The host owns the span. Only a declared, bounded shape may enter the graph.
    const bool valid = buffer.frames >= 0 && buffer.frames <= format_.maxFrames
        && buffer.inputChannels >= 0 && buffer.inputChannels <= 32
        && buffer.outputChannels >= 0 && buffer.outputChannels <= 32;
    const bool matching = valid && buffer.inputChannels == format_.inputChannels && buffer.outputChannels == format_.outputChannels;
    if (matching && buffer.frames > 0 && enabled_.load(std::memory_order_acquire))
        for (int channel = 0; channel < buffer.inputChannels; ++channel)
        {
            auto* copy = inputScratch_.data() + channel * format_.maxFrames;
            if (buffer.inputs && buffer.inputs[channel]) std::copy_n(buffer.inputs[channel], buffer.frames, copy);
            else std::fill_n(copy, buffer.frames, 0.0f);
        }
    if (buffer.outputs && buffer.frames > 0 && buffer.frames <= 65536
        && buffer.outputChannels >= 0 && buffer.outputChannels <= 32)
        for (int channel = 0; channel < buffer.outputChannels; ++channel)
            if (buffer.outputs[channel]) std::fill_n(buffer.outputs[channel], buffer.frames, 0.0f);
    if (!matching)
    {
        invalid_.store(invalid_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        interrupted(AudioStreamStop::formatChanged);
    }
    else if (enabled_.load(std::memory_order_acquire))
    {
        if (retired_.canPush())
            if (auto* next = pending_.exchange(nullptr, std::memory_order_acq_rel))
            {
                if (active_) { const auto pushed = retired_.push(active_); (void) pushed; }
                active_ = next;
                revision_.store(next->revision(), std::memory_order_relaxed);
            }
        GainChange event;
        for (int count = 0; count < 32 && gains_.peek(event); ++count)
        {
            if (active_ && event.revision > active_->revision()) break;
            const auto popped = gains_.pop(event); (void) popped;
            if (active_ && event.revision == active_->revision()) active_->setGain(event.index, event.value);
            else canceled_.store(canceled_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        if (active_ && buffer.frames > 0)
            active_->process({inputPointers_.data(), buffer.outputs, buffer.inputChannels, buffer.outputChannels, buffer.frames});
    }
    const auto ended = clock_();
    const auto elapsed = ended >= began ? ended - began : 0;
    lastNs_.store(elapsed, std::memory_order_relaxed);
    maxNs_.store(std::max(elapsed, maxNs_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    totalNs_.store(totalNs_.load(std::memory_order_relaxed) + elapsed, std::memory_order_relaxed);
    callbacks_.store(callbacks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    lastFrames_.store(valid && buffer.frames > 0 ? static_cast<std::uint64_t>(buffer.frames) : 0, std::memory_order_relaxed);
    if (valid && buffer.frames > 0)
    {
        frames_.store(frames_.load(std::memory_order_relaxed) + static_cast<std::uint64_t>(buffer.frames), std::memory_order_relaxed);
        if (format_.sampleRate > 0 && static_cast<double>(elapsed) > 1.0e9 * buffer.frames / format_.sampleRate)
            misses_.store(misses_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
}
AudioQuery<AudioEngineTelemetry> AudioEngine::telemetry() const
{
    if (const auto check = owner(); !check.succeeded)
        return {{}, {AudioErrorCode::wrongThread, check.error}};
    AudioEngineTelemetry result;
    result.callbacks = callbacks_.load(std::memory_order_relaxed);
    result.frames = frames_.load(std::memory_order_relaxed);
    result.activeRevision = revision_.load(std::memory_order_relaxed);
    result.callbackNanoseconds = lastNs_.load(std::memory_order_relaxed);
    result.maxCallbackNanoseconds = maxNs_.load(std::memory_order_relaxed);
    result.deadlineMisses = misses_.load(std::memory_order_relaxed);
    result.invalidBlocks = invalid_.load(std::memory_order_relaxed);
    result.canceledGainChanges = canceled_.load(std::memory_order_relaxed);
    result.running = enabled_.load(std::memory_order_acquire);
    result.stopReason = stopReason_.load(std::memory_order_relaxed);
    if (result.frames && format_.sampleRate > 0)
        result.averageCallbackLoad = static_cast<double>(totalNs_.load(std::memory_order_relaxed)) * format_.sampleRate
            / (1.0e9 * static_cast<double>(result.frames));
    // Last block load uses its actual frame count, not the negotiated nominal period.
    const auto lastFrames = lastFrames_.load(std::memory_order_relaxed);
    if (lastFrames && format_.sampleRate > 0)
        result.lastCallbackLoad = static_cast<double>(result.callbackNanoseconds) * format_.sampleRate
            / (1.0e9 * static_cast<double>(lastFrames));
    if (backend_)
    {
        const auto reported = backend_->streamDiagnostics();
        if (!reported.value) return {{}, reported.error};
        result.backendXruns = reported.value->backendXruns;
        if (reported.value->inputLatencyFrames) result.reportedInputLatencySeconds = *reported.value->inputLatencyFrames / format_.sampleRate;
        if (reported.value->outputLatencyFrames) result.reportedOutputLatencySeconds = *reported.value->outputLatencyFrames / format_.sampleRate;
    }
    return {result, {}};
}
}
