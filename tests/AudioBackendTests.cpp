#include "TestSupport.h"
#include <val/infrastructure/AudioBackends.h>
#include <juce_events/juce_events.h>
#include <chrono>
#include <thread>

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
    int scans{}, probes{};
    bool stopped{}, invalidateDuringProbe{};
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
    void stop() override { state_->stopped = true; state_->changed = {}; }
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

void testNative()
{
    std::vector<std::unique_ptr<IAudioBackend>> backends;
    backends.push_back(std::make_unique<AsioBackend>());
    backends.push_back(std::make_unique<WasapiExclusiveBackend>());
    backends.push_back(std::make_unique<WasapiSharedBackend>());
    backends.push_back(std::make_unique<CoreAudioBackend>());
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
        if (inventories < 4) return false;
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
}

int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juce;
    return runTests([&]
    {
        if (argc == 2 && std::string_view(argv[1]) == "--native") testNative();
        else testBackends();
    });
}
