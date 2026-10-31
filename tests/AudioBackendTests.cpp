#include "TestSupport.h"
#include <val/infrastructure/AudioBackends.h>
#include <val/audio/AudioEngine.h>
#include <juce_events/juce_events.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <array>

namespace
{
using namespace val::application;
using namespace val::infrastructure;

/** Observable fake hardware state survives discovery destruction for cancellation assertions. */
struct FakeState
{
    AudioDiscoveryInventory inventory{{{"out:usb", "USB output", AudioDirection::output, true}}, {"Test driver"}};
    AudioDeviceCapabilities capabilities{{}, {"Left", "Right"}, {44100, 48000}, {128, 256}, 256, {}, {}};
    std::function<void()> changed;
    AudioError scanError, probeError;
    int scans{}, probes{}, pairProbes{};
    bool stopped{}, invalidateDuringProbe{};
    bool failOpen{}, failStart{}, substituteBuffer{};
    IAudioRenderCallback* target{};
    AudioStreamDiagnostics stream;
    AudioStreamRequest openedRequest;
    int opens{}, panels{};
};

/** Deterministic injected discovery; no production discovery is replaced in native tests. */
class FakeDiscovery final : public IAudioDeviceDiscovery
{
public:
    explicit FakeDiscovery(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    void setChanged(std::function<void()> callback) override { state_->changed = std::move(callback); }
    AudioQuery<AudioDiscoveryInventory> scan() override
    {
        ++state_->scans;
        if (!state_->scanError.succeeded()) return {{}, state_->scanError};
        return {state_->inventory, {}};
    }
    AudioQuery<AudioDeviceCapabilities> probe(const DiscoveredAudioDevice&) override
    {
        ++state_->probes;
        if (state_->invalidateDuringProbe) state_->changed();
        if (!state_->probeError.succeeded()) return {{}, state_->probeError};
        return {state_->capabilities, {}};
    }
    AudioQuery<AudioDeviceCapabilities> probeConfiguration(const std::optional<DiscoveredAudioDevice>& input,
                                                          const DiscoveredAudioDevice& output) override
    {
        ++state_->pairProbes;
        require(input && input->direction == AudioDirection::input && output.direction == AudioDirection::output,
                "Pair probe resolves the actual input/output devices before touching the native seam.");
        auto capabilities = state_->capabilities;
        capabilities.inputChannels = {"Input left", "Input right"};
        capabilities.bufferFrames = {192};
        return {capabilities, {}};
    }
    AudioQuery<AudioStreamFormat> open(const std::optional<DiscoveredAudioDevice>&,
        const std::optional<DiscoveredAudioDevice>&, const AudioStreamRequest& request) override
    {
        ++state_->opens;
        state_->openedRequest = request;
        if (state_->failOpen) return {{}, {AudioErrorCode::unavailable, "Injected open failure."}};
        state_->stream = {true, false, 96, 144, 7};
        return {AudioStreamFormat{request.sampleRate, state_->substituteBuffer ? 256 : request.bufferFrames, request.maxFrames,
                                  request.inputChannels, request.outputChannels}, {}};
    }
    AudioError start(IAudioRenderCallback& target) override
    {
        if (!state_->stream.open || state_->failStart) return {AudioErrorCode::unavailable, "Injected start failure."};
        state_->target = &target; state_->stream.running = true;
        return {};
    }
    void stopStream() override
    {
        if (state_->target) state_->target->interrupted(AudioStreamStop::requested);
        state_->target = nullptr; state_->stream.running = false;
    }
    void closeStream() override { stopStream(); state_->stream.open = false; }
    void interruptStream(AudioStreamStop reason) override
    {
        if (state_->target) state_->target->interrupted(reason);
        state_->target = nullptr; state_->stream.running = false; state_->stream.open = false;
    }
    AudioStreamDiagnostics diagnostics() const override { return state_->stream; }
    AudioError showControlPanel(const DiscoveredAudioDevice&) override { ++state_->panels; return {}; }
    void stop() override { closeStream(); state_->stopped = true; state_->changed = {}; }
private:
    std::shared_ptr<FakeState> state_;
};

/** Executes owner-thread asynchronous test phases with one dispatch loop and a hard timeout. */
class AsyncSteps final : private juce::Timer
{
public:
    explicit AsyncSteps(std::vector<std::function<bool()>> steps) : steps_(std::move(steps)) {}
    void run()
    {
        startTimer(5);
        juce::MessageManager::getInstance()->runDispatchLoop();
        require(error_.empty(), error_);
        require(index_ == steps_.size(), "All asynchronous phases completed.");
    }
private:
    void timerCallback() override
    {
        try
        {
            require(std::chrono::steady_clock::now() - start_ < std::chrono::seconds(30), "Audio async test timed out.");
            if (index_ < steps_.size() && steps_[index_]()) ++index_;
            if (index_ < steps_.size()) return;
        }
        catch (const std::exception& error) { error_ = "Phase " + std::to_string(index_) + ": " + error.what(); }
        stopTimer();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }
    std::vector<std::function<bool()>> steps_;
    std::size_t index_{};
    std::string error_;
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
};

void testBackends()
{
    auto fake = std::make_shared<FakeState>();
    auto backend = std::make_unique<WasapiExclusiveBackend>(std::make_unique<FakeDiscovery>(fake));
    auto other = std::make_unique<WasapiExclusiveBackend>(std::make_unique<FakeDiscovery>(std::make_shared<FakeState>()));
    AudioDeviceToken original;
    AudioQuery<AudioDeviceCapabilities> answer;
    int notifications{}, completions{}, canceled{}, oldListener{}, removedListenerCount{};
    std::uint64_t removedAtGeneration{};
    bool inRequest{}, inListener{}, recursive{};
    AudioError workerError;
    bool workerRejected{};
    std::thread worker([&]
    {
        workerError = backend->refresh();
        workerRejected = backend->snapshot().error.code == AudioErrorCode::wrongThread
            && backend->setChangeCallback({}).code == AudioErrorCode::wrongThread
            && backend->queryCapabilities({}, [](auto) {}).code == AudioErrorCode::wrongThread
            && backend->shutdown().code == AudioErrorCode::wrongThread;
    });
    worker.join();
    require(workerError.code == AudioErrorCode::wrongThread && workerRejected && fake->scans == 0,
            "Foreign-thread requests are rejected without changing queued state.");
    require(backend->snapshot().value->error.code == AudioErrorCode::notScanned, "Initial status is not a fake successful inventory.");
    require(backend->setChangeCallback([&](const auto&)
    {
        recursive |= inRequest || inListener;
        inListener = true;
        ++notifications;
        if (notifications == 1) require(backend->refresh().succeeded(), "Listener may queue a later refresh.");
        inListener = false;
    }).succeeded(), "Register asynchronous observer.");
    const auto query = [&](AudioDeviceToken token)
    {
        inRequest = true;
        const auto accepted = backend->queryCapabilities(std::move(token), [&](auto result)
        {
            recursive |= inRequest || inListener;
            answer = std::move(result);
            ++completions;
        });
        inRequest = false;
        require(accepted.succeeded(), "Capability request accepted.");
    };

    AsyncSteps({
        [&]
        {
            inRequest = true;
            require(backend->refresh().succeeded() && backend->refresh().succeeded(), "Coalesce refresh requests.");
            inRequest = false;
            require(notifications == 0 && fake->scans == 0, "Refresh must not synchronously call drivers/listeners.");
            return true;
        },
        [&]
        {
            if (notifications < 2) return false;
            require(!recursive && fake->scans == 2, "Reentrant observer refresh is deferred, not recursive.");
            const auto snapshot = *backend->snapshot().value;
            require(snapshot.backend == AudioBackendKind::wasapiExclusive && snapshot.devices.size() == 1,
                    "Exclusive identity and real injected direction remain explicit.");
            original = snapshot.devices.front().token;
            query(original);
            return true;
        },
        [&]
        {
            if (completions != 1) return false;
            require(answer.value && answer.value->sampleRates == fake->capabilities.sampleRates
                    && answer.value->bufferFrames == fake->capabilities.bufferFrames
                    && answer.value->outputChannels.size() == 2 && !answer.value->negotiated,
                    "Capabilities map rates/buffers/channels without inventing negotiated settings.");
            fake->inventory.devices.clear();
            fake->changed();
            query(original);
            return true;
        },
        [&]
        {
            if (completions != 2 || notifications < 3) return false;
            require(!answer.value && answer.error.code == AudioErrorCode::staleDevice
                    && backend->snapshot().value->devices.empty() && fake->probes == 1,
                    "Unplug invalidates tokens before they can reach the driver.");
            fake->inventory.devices.push_back({"out:usb", "USB output", AudioDirection::output, true});
            fake->changed();
            return true;
        },
        [&]
        {
            if (notifications < 4) return false;
            query(original);
            return true;
        },
        [&]
        {
            if (completions != 3) return false;
            require(answer.error.code == AudioErrorCode::staleDevice, "Reappearing name does not revive an old token.");
            require(other->refresh().succeeded(), "Refresh another API instance.");
            return true;
        },
        [&]
        {
            if (other->snapshot().value->devices.empty()) return false;
            query(other->snapshot().value->devices.front().token);
            return true;
        },
        [&]
        {
            if (completions != 4) return false;
            require(answer.error.code == AudioErrorCode::staleDevice, "No cross-instance substitution even within the same API.");
            fake->probeError = {AudioErrorCode::unavailable, "Injected busy/removed driver."};
            query(backend->snapshot().value->devices.front().token);
            return true;
        },
        [&]
        {
            if (completions != 5) return false;
            require(!answer.value && answer.error.message == fake->probeError.message, "Driver errors survive mapping.");
            fake->probeError = {};
            fake->invalidateDuringProbe = true;
            query(backend->snapshot().value->devices.front().token);
            return true;
        },
        [&]
        {
            if (completions != 6) return false;
            require(answer.error.code == AudioErrorCode::staleDevice, "Disappearance during probe cannot return successful stale capabilities.");
            fake->invalidateDuringProbe = false;
            fake->scanError = {AudioErrorCode::unavailable, "Injected enumeration failure."};
            require(backend->refresh().succeeded(), "Request failing scan.");
            return true;
        },
        [&]
        {
            if (backend->snapshot().value->error.code != AudioErrorCode::unavailable) return false;
            require(backend->snapshot().value->devices.empty(), "Enumeration errors do not leave selectable stale devices.");
            fake->scanError = {};
            require(backend->setChangeCallback([&](const auto&) { ++oldListener; }).succeeded(), "Replace listener.");
            require(backend->refresh().succeeded(), "Queue refresh for listener replacement.");
            return true;
        },
        [&]
        {
            require(backend->setChangeCallback({}).succeeded(), "Remove pending observer.");
            removedListenerCount = oldListener;
            removedAtGeneration = backend->snapshot().value->generation;
            require(backend->refresh().succeeded(), "Refresh with removed observer.");
            return true;
        },
        [&]
        {
            if (backend->snapshot().value->generation == removedAtGeneration) return false;
            require(oldListener == removedListenerCount, "Removed observers do not receive subsequent notifications.");
            fake->inventory.devices.push_back(fake->inventory.devices.front());
            require(backend->refresh().succeeded(), "Refresh ambiguous inventory.");
            return true;
        },
        [&]
        {
            if (backend->snapshot().value->error.code != AudioErrorCode::unavailable) return false;
            require(backend->snapshot().value->devices.empty(), "Ambiguous IDs are never made selectable.");
            fake->inventory.devices.pop_back();
            require(backend->refresh().succeeded(), "Restore unambiguous inventory.");
            return true;
        },
        [&]
        {
            if (!backend->snapshot().value->error.succeeded()) return false;
            const auto token = backend->snapshot().value->devices.front().token;
            auto sharedToken = token;
            sharedToken.backend = AudioBackendKind::wasapiShared;
            query(sharedToken);
            return true;
        },
        [&]
        {
            if (completions != 7) return false;
            require(answer.error.code == AudioErrorCode::staleDevice, "Shared tokens cannot be used with exclusive APIs.");
            const auto token = backend->snapshot().value->devices.front().token;
            require(backend->queryCapabilities(token, {}).code == AudioErrorCode::invalidArgument, "Missing completion rejected.");
            for (int i = 0; i < 32; ++i)
                require(backend->queryCapabilities(token, [&](auto) { ++canceled; }).succeeded(), "Bounded queue accepts 32 requests.");
            require(backend->queryCapabilities(token, [&](auto) { ++canceled; }).code == AudioErrorCode::queueFull,
                    "Overload is explicit, never silently dropped.");
            require(backend->shutdown().succeeded() && backend->shutdown().succeeded(), "Orderly shutdown is idempotent.");
            require(fake->stopped && !fake->changed && canceled == 0
                    && backend->refresh().code == AudioErrorCode::stopped, "Shutdown quiesces source and cancels pending requests.");
            backend.reset();
            other.reset();
            return true;
        },
        [&]
        {
            auto state = std::make_shared<FakeState>();
            backend = std::make_unique<WasapiExclusiveBackend>(std::make_unique<FakeDiscovery>(state));
            require(backend->setChangeCallback([&](const auto&) { backend.reset(); }).succeeded(),
                    "Listener may destroy its owning backend.");
            require(backend->refresh().succeeded(), "Queue self-destroying listener.");
            return true;
        },
        [&] { return backend == nullptr; },
        [&] { require(canceled == 0 && !recursive, "No callback after teardown or during a caller/native callback."); return true; }
    }).run();
}

void testStreaming()
{
    using val::audio::AudioEngine;
    using val::audio::AudioGraph;
    using val::audio::AudioNodeType;
    auto fake = std::make_shared<FakeState>();
    fake->inventory.devices.push_back({"in:usb", "USB input", AudioDirection::input, true});
    WasapiSharedBackend backend(std::make_unique<FakeDiscovery>(fake));
    AudioEngine engine;
    AudioGraph graph{2, {{1, AudioNodeType::input}, {2, AudioNodeType::gain, 0.25f}, {3, AudioNodeType::output}},
                        {{1, 2}, {2, 3}}};
    AudioStreamRequest request;
    bool pairComplete{};
    AsyncSteps({
        [&] { require(backend.refresh().succeeded(), "Refresh streaming inventory."); return true; },
        [&]
        {
            const auto snapshot = *backend.snapshot().value;
            if (snapshot.devices.empty()) return false;
            require(backend.queryConfigurationCapabilities(snapshot.devices[1].token, snapshot.devices[0].token, [&](auto result)
            {
                require(result.value && result.value->inputChannels.size() == 2 && result.value->bufferFrames == std::vector{192},
                        "Combined JUCE candidates are preserved, not intersected from independently probed endpoint periods.");
                pairComplete = true;
            }).succeeded() && !pairComplete, "Pair probe runs asynchronously.");
            return true;
        },
        [&]
        {
            if (!pairComplete) return false;
            require(fake->pairProbes == 1 && fake->probes == 0, "Pair inspection uses one native driver instance.");
            const auto snapshot = *backend.snapshot().value;
            if (snapshot.devices.empty()) return false;
            request = {snapshot.devices[1].token, snapshot.devices[0].token, 48000, 128, 512, 2, 2, {4, 5}, {2, 3}};
            for (const auto& indices : std::vector<std::vector<int>>{{}, {3}, {3, 3}, {3, 2}, {-1, 0}, {0, 1024}})
            {
                auto malformed = request;
                malformed.outputChannelIndices = indices;
                require(!backend.openStream(malformed).value && fake->opens == 0,
                        "Missing, duplicate, unordered, negative and out-of-range identities reject before the native seam.");
            }
            auto invalid = request;
            invalid.output = invalid.input;
            require(!backend.openStream(invalid).value, "Wrong stream direction is rejected.");
            fake->failOpen = true;
            require(!engine.start(backend, request, graph, 1).succeeded && !fake->stream.open,
                    "Open failure cannot leave a running engine.");
            fake->failOpen = false;
            fake->substituteBuffer = true;
            require(!backend.openStream(request).value && !fake->stream.open,
                    "Driver buffer substitution inside capacity is rejected and closed, not silently accepted.");
            fake->substituteBuffer = false;
            fake->failStart = true;
            require(!engine.start(backend, request, graph, 1).succeeded && !fake->stream.open && !fake->target,
                    "Failed start closes prepared stream and releases target.");
            fake->failStart = false;
            auto badGraph = graph; badGraph.channels = 1;
            require(!engine.start(backend, request, badGraph, 1).succeeded && !fake->stream.open,
                    "Preparation failure closes successfully opened device.");
            require(engine.start(backend, request, graph, 1).succeeded, "Full engine -> abstraction -> injected platform lifecycle.");
            require(fake->openedRequest.inputChannelIndices == std::vector<int>{4, 5}
                    && fake->openedRequest.outputChannelIndices == std::vector<int>{2, 3},
                    "The adapter passes exact later-port masks through the injected native open seam.");
            std::array<float, 128> input, output;
            input.fill(0.5f); output.fill(99);
            const float* inputs[2]{input.data(), input.data()};
            float* outputs[2]{output.data(), nullptr};
            fake->target->process({inputs, outputs, 2, 2, 128});
            const auto report = *engine.telemetry().value;
            require(output[0] == 0.125f && report.callbacks == 1 && report.backendXruns == 7
                    && report.reportedInputLatencySeconds == 0.002 && report.reportedOutputLatencySeconds == 0.003
                    && !report.wholeProcessCpu, "Backend report and actual rendered samples retain separate meanings.");
            require(!backend.openStream(request).value, "Reconfiguration requires explicit stop/close.");
            fake->changed();
            return true;
        },
        [&]
        {
            if (fake->stream.open) return false;
            require(!fake->target && !engine.telemetry().value->running
                    && engine.telemetry().value->stopReason == AudioStreamStop::deviceLost,
                    "Hot-plug invalidation stops/closes and atomically notifies borrowed engine.");
            require(engine.stop().succeeded, "Loss cleanup reclaims graph only after callback quiescence.");
            const auto snapshot = *backend.snapshot().value;
            request.input = snapshot.devices[1].token; request.output = snapshot.devices[0].token;
            require(engine.start(backend, request, graph, 1).succeeded, "Explicitly reselect and restart after loss.");
            fake->inventory.devices.front().name = "Changed without native notification";
            require(backend.refresh().succeeded(), "Refresh uses the same inventory comparison as the polling fallback.");
            return true;
        },
        [&]
        {
            if (fake->stream.open) return false;
            require(!fake->target && engine.telemetry().value->stopReason == AudioStreamStop::deviceLost
                    && engine.stop().succeeded, "Inventory changes stop the stream even if a native notification was missed.");
            const auto snapshot = *backend.snapshot().value;
            request.input = snapshot.devices[1].token; request.output = snapshot.devices[0].token;
            require(engine.start(backend, request, graph, 1).succeeded, "Restart after inventory-only invalidation.");
            require(backend.shutdown().succeeded() && !fake->target && engine.stop().succeeded,
                    "Backend-first shutdown is quiescent and engine cleanup handles stopped backend.");
            return true;
        }
    }).run();
}

void testNativeSilence()
{
    WasapiSharedBackend backend;
    val::audio::AudioEngine engine;
    bool queried{}, opened{};
    AudioDeviceToken output;
    std::optional<AudioDeviceCapabilities> capabilities;
    std::string queryError;
    AsyncSteps({
        [&] { require(backend.refresh().succeeded(), "Refresh native shared output."); return true; },
        [&]
        {
            const auto snapshot = *backend.snapshot().value;
            if (snapshot.error.code == AudioErrorCode::notScanned) return false;
            require(snapshot.error.succeeded(), "WASAPI Shared must be available for explicit native silence test.");
            const auto found = std::find_if(snapshot.devices.begin(), snapshot.devices.end(),
                [](const auto& device) { return device.direction == AudioDirection::output && device.systemDefault; });
            require(found != snapshot.devices.end(), "No default shared output is available; native lifecycle cannot be qualified.");
            output = found->token;
            require(backend.queryCapabilities(output, [&](auto result)
            {
                queried = true;
                capabilities = std::move(result.value);
                queryError = result.error.message;
            }).succeeded(), "Explicitly probe default WASAPI Shared output.");
            return true;
        },
        [&]
        {
            if (!queried) return false;
            require(capabilities.has_value(), queryError);
            require(!capabilities->sampleRates.empty() && capabilities->defaultBufferFrames
                    && !capabilities->outputChannels.empty(), "Native output must report rate, period and channels.");
            const auto channels = static_cast<int>(capabilities->outputChannels.size());
            val::audio::AudioGraph silence; silence.channels = channels;
            const AudioStreamRequest request{{}, output, capabilities->sampleRates.front(),
                *capabilities->defaultBufferFrames, 4096, 0, channels, {}, legacyAudioChannels(channels)};
            const auto started = engine.start(backend, request, silence, 1);
            require(started.succeeded, started.error);
            opened = true;
            return true;
        },
        [&]
        {
            const auto report = *engine.telemetry().value;
            require(report.running && report.invalidBlocks == 0, "Native empty plan runs without shape faults.");
            if (report.callbacks < 100) return false;
            std::cout << "Native WASAPI Shared SILENCE ONLY: " << report.callbacks << " callbacks, "
                      << report.frames << " frames; last/max engine callback ns " << report.callbackNanoseconds
                      << "/" << report.maxCallbackNanoseconds << "; average engine deadline load "
                      << report.averageCallbackLoad << "; callback misses " << report.deadlineMisses;
            if (report.backendXruns) std::cout << "; backend xruns " << *report.backendXruns;
            else std::cout << "; backend xruns unknown";
            if (report.reportedOutputLatencySeconds) std::cout << "; backend-reported output latency s " << *report.reportedOutputLatencySeconds;
            std::cout << "; NOT physical roundtrip, NOT whole-process CPU.\n";
            require(engine.stop().succeeded && !backend.streamDiagnostics().value->open, "Native stop/close quiesces callbacks.");
            return true;
        }
    }).run();
    require(opened, "Explicit native silent lifecycle was exercised.");
}

void testNative()
{
    std::vector<std::unique_ptr<IAudioBackend>> backends;
    backends.push_back(std::make_unique<AsioBackend>());
    backends.push_back(std::make_unique<WasapiExclusiveBackend>());
    backends.push_back(std::make_unique<WasapiSharedBackend>());
    backends.push_back(std::make_unique<CoreAudioBackend>());
    backends.push_back(std::make_unique<DirectSoundBackend>());
    int inventories{};
    std::size_t sent{}, probes{}, successful{};
    std::vector<std::pair<IAudioBackend*, AudioDeviceInfo>> pending;
    std::vector<std::function<bool()>> steps;
    steps.push_back([&]
    {
        for (auto& backend : backends)
        {
            require(backend->setChangeCallback([&](const auto&) { ++inventories; }).succeeded(), "Observe native inventories.");
            require(backend->refresh().succeeded(), "Request native discovery.");
        }
        return true;
    });
    steps.push_back([&]
    {
        if (inventories < 5) return false;
        for (auto& backend : backends)
        {
            const auto state = *backend->snapshot().value;
            std::cout << "Backend " << static_cast<int>(state.backend) << ": "
                      << state.devices.size() << " endpoints; status " << static_cast<int>(state.error.code)
                      << " " << state.error.message << '\n';
            for (const auto& note : state.notes) std::cout << "  Note: " << note << '\n';
#if JUCE_WINDOWS
            if (state.backend == AudioBackendKind::coreAudio)
                require(state.error.code == AudioErrorCode::unsupportedPlatform, "CoreAudio must not masquerade as a Windows backend.");
            else if (state.backend != AudioBackendKind::asio)
                require(state.error.succeeded(), "Native WASAPI type must be available on this Windows host.");
            else
#if JUCE_ASIO
                require(state.error.succeeded(), "Enabled ASIO must expose the actual driver inventory.");
#else
                require(state.error.code == AudioErrorCode::disabled, "Disabled ASIO must not fake availability.");
#endif
#else
            if (state.backend != AudioBackendKind::coreAudio)
                require(state.error.code == AudioErrorCode::unsupportedPlatform, "Windows backends must not masquerade as CoreAudio.");
            else
                require(state.error.succeeded(), "Native CoreAudio type must be available on this macOS host.");
#endif
            // ASIO probes may briefly start/stop hardware inside JUCE; enumerate only in unattended tests.
            if (state.backend == AudioBackendKind::asio) continue;
            for (const auto& device : state.devices)
                pending.emplace_back(backend.get(), device);
        }
        return true;
    });
    steps.push_back([&]
    {
        if (probes != sent) return false;
        if (sent == pending.size()) return true;
        const auto& [backend, device] = pending[sent++];
        const auto name = device.name;
        const auto kind = backend->kind();
        require(backend->queryCapabilities(device.token, [&, name, kind](auto result)
        {
            ++probes;
            if (!result.value)
            {
                std::cout << "  Backend " << static_cast<int>(kind) << " " << name
                          << ": unavailable: " << result.error.message << '\n';
                return;
            }
            require(!result.value->negotiated, "Discovery cannot claim a negotiated native stream.");
            ++successful;
            std::cout << "  Backend " << static_cast<int>(kind) << " " << name << ": " << result.value->inputChannels.size()
                      << " in / " << result.value->outputChannels.size() << " out; rates";
            for (auto rate : result.value->sampleRates) std::cout << ' ' << rate;
            std::cout << "; frames";
            for (auto frames : result.value->bufferFrames) std::cout << ' ' << frames;
            std::cout << '\n';
        }).succeeded(), "Native query accepted.");
        return false;
    });
    steps.push_back([&]
    {
        std::cout << "Native capability queries: " << successful << "/" << probes << " successful.\n";
        for (auto& backend : backends) require(backend->shutdown().succeeded(), "Native listeners unregister cleanly.");
        return true;
    });
    AsyncSteps(std::move(steps)).run();
    const auto preferred = createPreferredAudioBackend();
#if JUCE_WINDOWS
    require(preferred->kind() == AudioBackendKind::asio, "Windows preference is ASIO even if no driver is installed.");
#else
    require(preferred->kind() == AudioBackendKind::coreAudio, "macOS preference is CoreAudio.");
#endif
}

/** Real subprocess transport, but the child fixture never invokes native discovery. */
int helperFixture(const std::string& mode, const juce::File& input, const juce::File& output)
{
    if (mode == "stall") { juce::Thread::sleep(30000); return 4; }
    if (mode == "crash") return 4;
    if (mode == "invalid") return output.replaceWithText("{") ? 0 : 3;
    const auto request = juce::JSON::parse(input.loadFileAsString());
    const auto operation = request["operation"].toString();
    const auto response = operation == "scan"
        ? R"({"code":0,"message":"","devices":[{"key":"out:usb","name":"USB output","direction":1,"default":true}],"notes":["Isolated test fixture; no hardware"]})"
        : operation == "panel" ? R"({"code":0,"message":""})"
        : R"({"code":0,"message":"","inputs":[],"outputs":["Main L","Main R","Virtual L","Virtual \u97f3 R"],"rates":[48000],"buffers":[128],"defaultBuffer":128,"notes":["No hardware"],"panel":true})";
    return output.replaceWithText(response) ? 0 : 3;
}
void testHelperIsolation(const std::string& mode)
{
    for (const auto* malformed : {"{", R"({"backend":99,"operation":"scan"})", R"({"backend":0,"operation":"probe"})"})
    {
        const auto response = juce::JSON::parse(juce::String::fromUTF8(runAudioDiscoveryHelper(malformed).c_str()));
        require(static_cast<int>(response["code"]) == static_cast<int>(AudioErrorCode::invalidArgument),
                "Malformed helper protocol rejects before any native type or driver is created.");
    }
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("val-helper-tests-" + juce::Uuid().toString());
    require(directory.createDirectory().wasOk(), "Create isolated helper protocol fixture directory.");
    if (mode != "cancel")
    {
        auto fake = std::make_shared<FakeState>();
        AudioDiscoveryHelperOptions options{
            (mode == "missing" ? directory.getChildFile("missing-helper.exe")
                : juce::File::getSpecialLocation(juce::File::currentExecutableFile)).getFullPathName().toStdString(),
            directory.getFullPathName().toStdString(), {"--helper-fixture", mode}, mode == "stall" ? 150 : 5000};
        WasapiSharedBackend backend(std::make_unique<FakeDiscovery>(fake), options);
        bool notified{}, probed{}, panelComplete{};
        int heartbeats{};
        require(backend.setChangeCallback([&](const auto&) { notified = true; }).succeeded()
                && backend.refresh().succeeded() && !notified, "Helper enumeration is asynchronous.");
        AsyncSteps({
            [&]
            {
                ++heartbeats;
                if (!notified) return false;
                require(heartbeats >= 2 && fake->scans == 0 && fake->probes == 0 && fake->opens == 0,
                        "UI message loop remains responsive while the helper runs; no native source is probed or opened in tests.");
                const auto snapshot = *backend.snapshot().value;
                if (mode != "success")
                {
                    require(!snapshot.error.succeeded() && snapshot.devices.empty()
                            && snapshot.error.message.find(mode == "stall" ? "timed out" : mode == "missing" ? "could not start"
                                : mode == "invalid" ? "invalid data" : "without a result") != std::string::npos,
                            "Hung/crashed helpers produce actionable bounded failures, not success-shaped empty inventories.");
                    return true;
                }
                require(snapshot.devices.size() == 1 && snapshot.devices.front().name == "USB output", "IPC preserves labeled endpoint identities.");
                require(backend.queryConfigurationCapabilities({}, snapshot.devices.front().token, [&](auto result)
                {
                    require(result.value && result.value->outputChannels == std::vector<std::string>{"Main L", "Main R", "Virtual L", "Virtual \xE9\x9F\xB3 R"}
                            && result.value->sampleRates == std::vector<double>{48000} && result.value->bufferFrames == std::vector<int>{128}
                            && result.value->hasControlPanel && !result.value->negotiated,
                            "IPC preserves driver names, candidates and panel support without negotiating a stream.");
                    probed = true;
                }).succeeded() && !probed, "Isolated pair query returns later.");
                return true;
            },
            [&]
            {
                if (mode != "success") return true;
                if (!probed) return false;
                const auto token = backend.snapshot().value->devices.front().token;
                require(backend.showControlPanel(token, [&](AudioError error)
                { require(error.succeeded(), "Fixture panel completes."); panelComplete = true; }).succeeded()
                        && !panelComplete && fake->opens == 0, "Isolated panel request never opens an application stream.");
                return true;
            },
            [&]
            {
                if (mode == "success" && !panelComplete) return false;
                require(backend.shutdown().succeeded(), "Helper shutdown cancels without native calls.");
                return true;
            }
        }).run();
    }
    if (mode == "cancel")
    {
        auto fake = std::make_shared<FakeState>();
        auto backend = std::make_unique<WasapiSharedBackend>(std::make_unique<FakeDiscovery>(fake),
            AudioDiscoveryHelperOptions{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString(),
                directory.getFullPathName().toStdString(), {"--helper-fixture", "stall"}, 150});
        int callbacks{};
        require(backend->setChangeCallback([&](const auto&) { ++callbacks; }).succeeded() && backend->refresh().succeeded(),
                "Queue cancellable helper.");
        int ticks{};
        AsyncSteps({
            [&] { if (++ticks < 4) return false; backend.reset(); return true; },
            [&]
            {
                if (++ticks < 80) return false;
                require(callbacks == 0 && directory.getNumberOfChildFiles(juce::File::findFiles) == 0,
                        "Destruction cancels helper process work, suppresses callbacks and cleans owned IPC files.");
                return true;
            }
        }).run();
    }
    require(directory.getNumberOfChildFiles(juce::File::findFiles) == 0 && directory.deleteFile(), "Clean exact helper fixture directory.");
}
}

int main(int argc, char** argv)
{
    if (argc == 5 && std::string_view(argv[1]) == "--helper-fixture")
        return helperFixture(argv[2], juce::File(juce::String::fromUTF8(argv[3])), juce::File(juce::String::fromUTF8(argv[4])));
    const juce::ScopedJuceInitialiser_GUI juce;
    return runTests([&]
    {
        if (argc == 2 && std::string_view(argv[1]) == "--native") testNative();
        else if (argc == 2 && std::string_view(argv[1]) == "--streams") testStreaming();
        else if (argc == 2 && std::string_view(argv[1]) == "--native-silence") testNativeSilence();
        else if (argc == 3 && std::string_view(argv[1]) == "--helper-check") testHelperIsolation(argv[2]);
        else
        {
            testBackends();
            for (const auto* mode : {"success", "stall", "crash", "missing", "invalid", "cancel"})
            {
                juce::ChildProcess child;
                require(child.start(juce::StringArray{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
                    "--helper-check", mode}) && child.waitForProcessToFinish(10000), "Run isolated non-native helper regression.");
                const auto output = child.readAllProcessOutput().toStdString();
                require(child.getExitCode() == 0, output);
            }
        }
    });
}
