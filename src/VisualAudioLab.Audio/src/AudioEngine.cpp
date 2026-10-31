#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <new>
#include <limits>

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
    position_ = 0; scheduleFloor_ = 0; appliedEvents_ = 0; canceledEvents_ = 0; missedEvents_ = 0;
    lastEventSequence_ = 0; lastEventFrame_ = 0;
    nextSequence_ = 0; lastScheduledFrame_ = 0;
}
Result AudioEngine::startOffline(AudioStreamFormat format, const AudioGraph& graph, std::uint64_t revision)
{
    if (const auto check = owner(); !check.succeeded) return check;
    if (configured_) return Result::failure("Stop the existing engine session before reconfiguring.");
    if (epoch_ == std::numeric_limits<std::uint64_t>::max()) return Result::failure("Audio session epoch exhausted; create a new engine.");
    auto prepared = PreparedAudioGraph::prepare(graph, format, revision);
    if (!prepared.value) return Result::failure(prepared.error.message);
    try { inputScratch_.resize(static_cast<std::size_t>(format.maxFrames) * static_cast<std::size_t>(format.inputChannels)); }
    catch (const std::bad_alloc&) { return Result::failure("Insufficient memory while preparing audio input staging."); }
    for (int channel = 0; channel < format.inputChannels; ++channel)
        inputPointers_[channel] = inputScratch_.data() + channel * format.maxFrames;
    format_ = format;
    active_ = prepared.value->release();
    latest_ = active_;
    acceptedRevision_ = revision;
    configured_ = true;
    ++epoch_;
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
    lastScheduledFrame_ = 0;
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
    return setParameter(revision, node, 0, gain);
}
Result AudioEngine::setParameter(std::uint64_t revision, std::uint64_t node, std::uint16_t parameter,
                                float value, ParameterAction action)
{
    if (const auto check = owner(); !check.succeeded) return check;
    std::size_t index{};
    if (!configured_ || !enabled_.load(std::memory_order_acquire) || revision != acceptedRevision_
        || !latest_->parameterIndex(node, parameter, value, action, index))
        return Result::failure("Live parameter needs a running latest revision, valid node/parameter/action and in-range finite value.");
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
        return Result::failure("Parameter acceptance sequence exhausted; restart the engine session.");
    if (!gains_.push({revision, index, value, parameter, action, nextSequence_ + 1}))
        return Result::failure("Live parameter queue is full (32); retry after processing.");
    ++nextSequence_;
    return Result::success();
}
Result AudioEngine::scheduleParameter(std::uint64_t epoch, std::uint64_t revision, std::uint64_t node,
    std::uint16_t parameter, std::uint64_t frame, float value, ParameterAction action)
{
    if (const auto check = owner(); !check.succeeded) return check;
    std::size_t index{};
    if (!configured_ || !enabled_.load(std::memory_order_acquire) || epoch != epoch_ || revision != acceptedRevision_
        || !latest_->parameterIndex(node, parameter, value, action, index))
        return Result::failure("Scheduled event has an inactive epoch/revision or an invalid node/parameter/value/action.");
    const auto floor = scheduleFloor_.load(std::memory_order_acquire);
    if (frame < floor || frame < lastScheduledFrame_ || frame - floor > static_cast<std::uint64_t>(format_.sampleRate * 10)
        || nextSequence_ == std::numeric_limits<std::uint64_t>::max())
        return Result::failure("Schedule in nondecreasing frame order, ahead of the admitted render span and at most ten seconds ahead.");
    if (!scheduled_.push({{revision, index, value, parameter, action, nextSequence_ + 1}, frame}))
        return Result::failure("Scheduled event queue is full (128); retain future events on the control thread and retry.");
    ++nextSequence_;
    lastScheduledFrame_ = frame;
    return Result::success();
}
Result AudioEngine::collectRetired()
{
    if (const auto check = owner(); !check.succeeded) return check;
    PreparedAudioGraph* old{};
    while (retired_.pop(old)) delete old;
    return Result::success();
}
AudioQuery<AnalyzerSnapshot> AudioEngine::analyzerSnapshot(std::uint64_t revision, std::uint64_t node) const
{
    if (const auto check = owner(); !check.succeeded) return {{}, {AudioErrorCode::wrongThread, check.error}};
    if (!configured_ || revision != acceptedRevision_)
        return {{}, {AudioErrorCode::invalidArgument, "Analyzer observation requires the latest accepted session revision."}};
    const auto snapshot = latest_->analyzerSnapshot(node);
    if (!snapshot) return {{}, {AudioErrorCode::invalidArgument, "No Analyzer node with that identity in this revision."}};
    return {*snapshot, {}};
}
AudioQuery<script::RuntimeFault> AudioEngine::scriptFault(std::uint64_t revision, std::uint64_t node) const
{
    if (const auto check = owner(); !check.succeeded) return {{}, {AudioErrorCode::wrongThread, check.error}};
    if (!configured_ || revision != acceptedRevision_)
        return {{}, {AudioErrorCode::invalidArgument, "Script fault observation requires the latest accepted session revision."}};
    const auto fault = latest_->scriptFault(node);
    if (!fault) return {{}, {AudioErrorCode::invalidArgument, "No Script node with that identity in this revision."}};
    return {*fault, {}};
}
void AudioEngine::clearPlans()
{
    delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    delete active_; active_ = nullptr; latest_ = nullptr;
    const auto collected = collectRetired();
    assert(collected.succeeded);
    GainChange event;
    while (gains_.pop(event)) {}
    ScheduledChange scheduled;
    while (scheduled_.pop(scheduled)) {}
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
        const auto beganFrame = position_.load(std::memory_order_relaxed);
        const auto endFrame = beganFrame + static_cast<std::uint64_t>(buffer.frames);
        if (endFrame < beganFrame) { interrupted(AudioStreamStop::formatChanged); return; }
        scheduleFloor_.store(endFrame, std::memory_order_release);
        const auto scheduledCount = scheduled_.available();
        if (retired_.canPush())
            if (auto* next = pending_.exchange(nullptr, std::memory_order_acq_rel))
            {
                if (active_) { const auto pushed = retired_.push(active_); (void) pushed; }
                active_ = next;
                revision_.store(next->revision(), std::memory_order_relaxed);
            }
        const auto liveCount = gains_.available();
        int offset{};
        const AudioBuffer staged{inputPointers_.data(), buffer.outputs, buffer.inputChannels, buffer.outputChannels, buffer.frames};
        std::size_t liveConsumed{}, scheduledConsumed{};
        for (std::size_t count = 0; buffer.frames > 0 && count < scheduledCount + liveCount; ++count)
        {
            GainChange live;
            ScheduledChange scheduled;
            const auto haveLive = liveConsumed < liveCount && gains_.peek(live);
            const auto haveScheduled = scheduledConsumed < scheduledCount && scheduled_.peek(scheduled);
            if (haveLive && (!active_ || live.revision < active_->revision()))
            {
                const auto popped = gains_.pop(live); (void) popped; ++liveConsumed;
                canceled_.store(canceled_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                canceledEvents_.store(canceledEvents_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                continue;
            }
            if (haveScheduled && (!active_ || scheduled.change.revision < active_->revision()))
            {
                const auto popped = scheduled_.pop(scheduled); (void) popped; ++scheduledConsumed;
                canceledEvents_.store(canceledEvents_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                continue;
            }
            if (haveScheduled && active_ && scheduled.change.revision == active_->revision() && scheduled.frame < beganFrame)
            {
                const auto popped = scheduled_.pop(scheduled); (void) popped; ++scheduledConsumed;
                missedEvents_.store(missedEvents_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                continue;
            }
            const auto liveReady = haveLive && active_ && live.revision == active_->revision();
            const auto scheduledReady = haveScheduled && active_ && scheduled.change.revision == active_->revision()
                && scheduled.frame < endFrame;
            if (!liveReady && !scheduledReady) break;
            const auto takeLive = liveReady && (!scheduledReady || scheduled.frame > beganFrame
                || live.sequence < scheduled.change.sequence);
            GainChange change;
            std::uint64_t frame{};
            if (takeLive)
            {
                const auto popped = gains_.pop(change); (void) popped; ++liveConsumed;
                frame = beganFrame;
            }
            else
            {
                const auto popped = scheduled_.pop(scheduled); (void) popped; ++scheduledConsumed;
                change = scheduled.change;
                frame = scheduled.frame;
            }
            const auto eventOffset = static_cast<int>(frame - beganFrame);
            if (eventOffset > offset) active_->process(staged, offset, eventOffset - offset);
            offset = eventOffset;
            active_->parameterEvent(change.index, change.parameter, change.value, change.action);
            appliedEvents_.store(appliedEvents_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            lastEventSequence_.store(change.sequence, std::memory_order_relaxed);
            lastEventFrame_.store(frame, std::memory_order_relaxed);
        }
        if (active_ && buffer.frames > offset) active_->process(staged, offset, buffer.frames - offset);
        if (active_ && buffer.frames > 0) active_->capture(buffer.frames, epoch_, beganFrame);
        position_.store(endFrame, std::memory_order_release);
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
AudioQuery<AudioStreamFormat> AudioEngine::streamFormat() const
{
    if (const auto check = owner(); !check.succeeded) return {{}, {AudioErrorCode::wrongThread, check.error}};
    if (!configured_) return {{}, {AudioErrorCode::stopped, "No configured audio stream."}};
    return {format_, {}};
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
    result.epoch = epoch_;
    result.samplePosition = position_.load(std::memory_order_acquire);
    result.appliedEvents = appliedEvents_.load(std::memory_order_relaxed);
    result.canceledEvents = canceledEvents_.load(std::memory_order_relaxed);
    result.missedEvents = missedEvents_.load(std::memory_order_relaxed);
    result.lastEventSequence = lastEventSequence_.load(std::memory_order_relaxed);
    result.lastEventFrame = lastEventFrame_.load(std::memory_order_relaxed);
    if (configured_) result.latestPlanNumericalFaults = latest_->numericalFaults();
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
