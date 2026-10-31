#include <val/infrastructure/AudioBackends.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#endif

namespace val::infrastructure
{
using namespace application;
namespace
{
AudioError failure(AudioErrorCode code, std::string message) { return {code, std::move(message)}; }
bool onOwnerThread()
{
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager && manager->isThisTheMessageThread();
}

/** Owns a JUCE driver type, temporary probes and an explicitly requested quiescent-lifetime stream. */
class NativeDiscovery final : public IAudioDeviceDiscovery, private juce::AudioIODeviceType::Listener,
                              private juce::AsyncUpdater, private juce::AudioIODeviceCallback
{
public:
    explicit NativeDiscovery(AudioBackendKind kind, bool deferType = false) : kind_(kind)
    {
        if (!deferType) initialize();
    }
    void initialize()
    {
        initialized_ = true;
#if JUCE_WINDOWS
        switch (kind_)
        {
            case AudioBackendKind::asio:
#if JUCE_ASIO
                type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
#else
                error_ = failure(AudioErrorCode::disabled, "ASIO is disabled. Reconfigure with VAL_ENABLE_ASIO=ON.");
#endif
                break;
            case AudioBackendKind::wasapiExclusive:
                type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::exclusive));
                break;
            case AudioBackendKind::wasapiShared:
                type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
                break;
            case AudioBackendKind::directSound:
                type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_DirectSound());
                break;
            case AudioBackendKind::coreAudio:
                error_ = failure(AudioErrorCode::unsupportedPlatform, "CoreAudio requires macOS.");
                break;
        }
#elif JUCE_MAC
        if (kind_ == AudioBackendKind::coreAudio)
            type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
        else
            error_ = failure(AudioErrorCode::unsupportedPlatform, "ASIO, WASAPI and DirectSound require Windows.");
#else
        error_ = failure(AudioErrorCode::unsupportedPlatform, "Native audio requires Windows or macOS.");
#endif
        if (!type_ && error_.succeeded())
            error_ = failure(AudioErrorCode::unavailable, "JUCE could not create the requested native audio API.");
        if (type_) type_->addListener(this);
#if JUCE_WINDOWS
        if (type_ && kind_ == AudioBackendKind::asio)
        {
            CM_NOTIFY_FILTER filter{};
            filter.cbSize = sizeof(filter);
            filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
            filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
            if (CM_Register_Notification(&filter, this, &pnpChanged, &notification_) != CR_SUCCESS)
                notificationNote_ = "Windows PnP subscription failed; only periodic driver inventory and explicit probes are available.";
        }
#endif
    }

    ~NativeDiscovery() override { stop(); }
    void setChanged(std::function<void()> changed) override { changed_ = std::move(changed); }
    void stop() override
    {
        initialized_ = true;
        closeStream();
#if JUCE_WINDOWS
        if (notification_)
        {
            CM_Unregister_Notification(notification_);
            notification_ = nullptr;
        }
#endif
        if (type_) type_->removeListener(this);
        type_.reset();
        cancelPendingUpdate();
        changed_ = {};
    }

    AudioQuery<AudioDiscoveryInventory> scan() override
    {
        if (!error_.succeeded()) return {{}, error_};
        if (!type_) return {{}, failure(AudioErrorCode::stopped, "Native discovery has stopped.")};
        type_->scanForDevices();
        AudioDiscoveryInventory result;
        const auto append = [&](bool input, AudioDirection direction)
        {
            const auto names = type_->getDeviceNames(input);
            const auto defaultIndex = kind_ == AudioBackendKind::asio ? -1 : type_->getDefaultDeviceIndex(input);
            for (int i = 0; i < names.size(); ++i)
            {
                const auto name = names[i].toStdString();
                result.devices.push_back({std::to_string(static_cast<int>(direction)) + ":" + name,
                                          name, direction, i == defaultIndex});
            }
        };
        if (type_->hasSeparateInputsAndOutputs())
        {
            append(true, AudioDirection::input);
            append(false, AudioDirection::output);
        }
        else append(false, AudioDirection::duplex);
        result.notes.push_back("JUCE exposes name-based locators, not persistent hardware UIDs. Rediscover before using old tokens.");
        if (kind_ == AudioBackendKind::asio)
        {
            result.notes.push_back("ASIO lists installed drivers, not guaranteed connected hardware. PnP changes invalidate tokens; probe to check driver availability.");
            if (!notificationNote_.empty()) result.notes.push_back(notificationNote_);
        }
        if (result.devices.empty())
            result.notes.push_back("No devices reported. Install/connect a compatible device or driver and refresh; no alternate API was selected.");
        return {std::move(result), {}};
    }

    AudioQuery<AudioDeviceCapabilities> probe(const DiscoveredAudioDevice& item) override
    {
        return inspectDevice(item.direction != AudioDirection::output ? std::optional(item) : std::nullopt,
                             item.direction != AudioDirection::input ? std::optional(item) : std::nullopt);
    }
    AudioQuery<AudioDeviceCapabilities> probeConfiguration(
        const std::optional<DiscoveredAudioDevice>& input, const DiscoveredAudioDevice& output) override
    {
        return inspectDevice(input, output);
    }
    AudioQuery<AudioDeviceCapabilities> inspectDevice(const std::optional<DiscoveredAudioDevice>& input,
                                                       const std::optional<DiscoveredAudioDevice>& output)
    {
        if (!type_) return {{}, error_.succeeded() ? failure(AudioErrorCode::stopped, "Discovery stopped.") : error_};
        const auto name = [](const auto& item) { return item ? juce::String::fromUTF8(item->name.c_str()) : juce::String{}; };
        std::unique_ptr<juce::AudioIODevice> device(type_->createDevice(name(output), name(input)));
        if (!device)
            return {{}, failure(AudioErrorCode::unavailable, "Device disappeared, is busy, or its driver could not initialize. Refresh and retry.")};
        if (const auto error = device->getLastError(); error.isNotEmpty())
            return {{}, failure(AudioErrorCode::unavailable, "Driver capability query failed: " + error.toStdString())};
        AudioDeviceCapabilities result;
        result.hasControlPanel = device->hasControlPanel();
        if (input)
            for (const auto& channel : device->getInputChannelNames()) result.inputChannels.push_back(channel.toStdString());
        if (output)
            for (const auto& channel : device->getOutputChannelNames()) result.outputChannels.push_back(channel.toStdString());
        bool invalidCandidates{};
        for (auto rate : device->getAvailableSampleRates())
        {
            if (std::isfinite(rate) && rate > 0) result.sampleRates.push_back(rate);
            else invalidCandidates = true;
        }
        for (auto frames : device->getAvailableBufferSizes())
        {
            if (frames > 0) result.bufferFrames.push_back(frames);
            else invalidCandidates = true;
        }
        const auto defaultFrames = device->getDefaultBufferSize();
        if (defaultFrames > 0) result.defaultBufferFrames = defaultFrames;
        if (result.inputChannels.empty() && result.outputChannels.empty())
            return {{}, failure(AudioErrorCode::unavailable, "Driver reported no usable channels. The device may be disconnected or unavailable.")};
        result.notes.push_back("Queried JUCE/driver candidates only. Formats, channel layouts and periods are interdependent and require explicit open-time negotiation.");
        result.notes.push_back("No application stream/settings negotiated. Device initialization may touch hardware; these are not current stream settings.");
        if (kind_ == AudioBackendKind::asio)
            result.notes.push_back("JUCE ASIO initialization may allocate dummy buffers and briefly start/stop the driver. Automatic inspection runs in an isolated helper, not the application stream.");
        if (kind_ == AudioBackendKind::wasapiShared)
            result.notes.push_back("Shared-mode rates allow Windows mixer conversion. Buffer frames select the JUCE callback block, not the host-managed hardware period.");
        if (kind_ == AudioBackendKind::wasapiExclusive)
            result.notes.push_back("Exclusive candidates do not guarantee access; Windows policy or another client may reject a future exclusive open.");
        if (result.sampleRates.empty() || result.bufferFrames.empty())
            result.notes.push_back("The driver did not report all rate/period candidates; absent lists mean unknown, not universal support.");
        if (invalidCandidates)
            result.notes.push_back("The driver returned nonpositive or nonfinite rate/period candidates; invalid entries were excluded.");
        return {std::move(result), {}};
    }

    AudioQuery<AudioStreamFormat> open(const std::optional<DiscoveredAudioDevice>& input,
        const std::optional<DiscoveredAudioDevice>& output, const AudioStreamRequest& request) override
    {
        if (device_) return {{}, failure(AudioErrorCode::invalidArgument, "Close the existing stream before opening another.")};
        if (!initialized_) initialize();
        if (!type_) return {{}, error_.succeeded() ? failure(AudioErrorCode::stopped, "Discovery stopped.") : error_};
        type_->scanForDevices();
        const auto name = [](const auto& item) { return item ? juce::String::fromUTF8(item->name.c_str()) : juce::String{}; };
        auto device = std::unique_ptr<juce::AudioIODevice>(type_->createDevice(name(output), name(input)));
        if (!device) return {{}, failure(AudioErrorCode::unavailable, "Selected device disappeared or its driver could not initialize.")};
        if ((!request.inputChannelIndices.empty() && request.inputChannelIndices.back() >= device->getInputChannelNames().size())
            || (!request.outputChannelIndices.empty() && request.outputChannelIndices.back() >= device->getOutputChannelNames().size()))
            return {{}, failure(AudioErrorCode::invalidArgument, "Requested channels are not available; no channel truncation is performed.")};
        juce::BigInteger inputs, outputs;
        for (int index : request.inputChannelIndices) inputs.setBit(index);
        for (int index : request.outputChannelIndices) outputs.setBit(index);
        if (const auto error = device->open(inputs, outputs, request.sampleRate, request.bufferFrames); error.isNotEmpty())
            return {{}, failure(AudioErrorCode::unavailable, "Native stream open failed: " + error.toStdString())};
        if (device->getActiveInputChannels() != inputs || device->getActiveOutputChannels() != outputs)
        {
            device->close();
            return {{}, failure(AudioErrorCode::unavailable, "Driver changed the selected channel identities; no substitution is accepted.")};
        }
        AudioStreamFormat format{device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), request.maxFrames,
                                  device->getActiveInputChannels().countNumberOfSetBits(),
                                  device->getActiveOutputChannels().countNumberOfSetBits()};
        format_ = format;
        device_ = std::move(device);
        fault_.store(false, std::memory_order_relaxed);
        return {format, {}};
    }

    AudioError start(IAudioRenderCallback& target) override
    {
        if (!device_) return failure(AudioErrorCode::unavailable, "Open a stream before starting.");
        if (target_.load(std::memory_order_acquire)) return failure(AudioErrorCode::invalidArgument, "Stream already started.");
        fault_.store(false, std::memory_order_relaxed);
        stopping_.store(false, std::memory_order_relaxed);
        target_.store(&target, std::memory_order_release);
        device_->start(this);
        if (!device_->isPlaying() || fault_.load(std::memory_order_acquire))
        {
            stopStream();
            return failure(AudioErrorCode::unavailable, "Native stream did not start or its format changed. Close, refresh and retry.");
        }
        return {};
    }
    AudioError showControlPanel(const DiscoveredAudioDevice& item) override
    {
        if (!type_ || device_) return failure(AudioErrorCode::invalidArgument, "Stop and close audio before opening the driver panel.");
        const auto name = juce::String::fromUTF8(item.name.c_str());
        std::unique_ptr<juce::AudioIODevice> device(type_->createDevice(name, item.direction == AudioDirection::duplex ? name : juce::String{}));
        if (!device || !device->hasControlPanel()) return failure(AudioErrorCode::unavailable, "This driver has no control panel.");
        // JUCE ASIO's return value is based on elapsed time, not a reliable success indicator.
        device->showControlPanel();
        return {};
    }
    void stopStream() override
    {
        stopping_.store(true, std::memory_order_release);
        if (device_) device_->stop(); // Native JUCE stop establishes callback quiescence.
        running_.store(false, std::memory_order_release);
        if (auto* target = target_.exchange(nullptr, std::memory_order_acq_rel))
            target->interrupted(AudioStreamStop::requested);
    }
    void closeStream() override
    {
        stopStream();
        if (device_) device_->close();
        device_.reset();
    }
    void interruptStream(AudioStreamStop reason) override
    {
        if (auto* target = target_.load(std::memory_order_acquire)) target->interrupted(reason);
        // Do not replace the first fault reason with ordinary requested stop.
        stopping_.store(true, std::memory_order_release);
        if (device_) device_->stop();
        target_.store(nullptr, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        if (device_) device_->close();
        device_.reset();
    }
    AudioStreamDiagnostics diagnostics() const override
    {
        AudioStreamDiagnostics result;
        result.open = device_ != nullptr;
        result.running = running_.load(std::memory_order_acquire) && !fault_.load(std::memory_order_acquire);
        if (device_)
        {
            const auto input = device_->getInputLatencyInSamples(), output = device_->getOutputLatencyInSamples();
            const auto xruns = device_->getXRunCount();
            if (format_.inputChannels > 0 && input >= 0) result.inputLatencyFrames = input;
            if (format_.outputChannels > 0 && output >= 0) result.outputLatencyFrames = output;
            if (xruns >= 0) result.backendXruns = static_cast<std::uint64_t>(xruns);
        }
        return result;
    }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int inputChannels, float* const* outputs,
                                          int outputChannels, int frames, const juce::AudioIODeviceCallbackContext&) override
    {
        if (auto* target = target_.load(std::memory_order_acquire); target && !fault_.load(std::memory_order_acquire))
            target->process({inputs, outputs, inputChannels, outputChannels, frames});
        else if (frames > 0 && frames <= 65536 && outputChannels >= 0 && outputChannels <= 32)
            for (int channel = 0; channel < outputChannels; ++channel)
                if (outputs && outputs[channel]) std::fill_n(outputs[channel], frames, 0.0f);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const auto invalid = device->getCurrentSampleRate() != format_.sampleRate
            || device->getCurrentBufferSizeSamples() < 1 || device->getCurrentBufferSizeSamples() > format_.maxFrames;
        fault_.store(invalid, std::memory_order_release);
        running_.store(!invalid, std::memory_order_release);
        if (invalid)
            if (auto* target = target_.load(std::memory_order_acquire)) target->interrupted(AudioStreamStop::formatChanged);
    }
    void audioDeviceStopped() override
    {
        running_.store(false, std::memory_order_release);
        if (!stopping_.load(std::memory_order_acquire))
        {
            fault_.store(true, std::memory_order_release);
            if (auto* target = target_.load(std::memory_order_acquire)) target->interrupted(AudioStreamStop::deviceLost);
        }
    }
    void audioDeviceError(const juce::String&) override
    {
        fault_.store(true, std::memory_order_release);
        if (auto* target = target_.load(std::memory_order_acquire)) target->interrupted(AudioStreamStop::deviceError);
    }
    void audioDeviceListChanged() override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { if (changed_) changed_(); }
#if JUCE_WINDOWS
    static DWORD CALLBACK pnpChanged(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION,
                                     PCM_NOTIFY_EVENT_DATA, DWORD)
    {
        static_cast<NativeDiscovery*>(context)->triggerAsyncUpdate();
        return ERROR_SUCCESS;
    }
    HCMNOTIFICATION notification_{};
#endif
    AudioBackendKind kind_;
    bool initialized_{};
    AudioError error_;
    std::unique_ptr<juce::AudioIODeviceType> type_;
    std::function<void()> changed_;
    std::string notificationNote_;
    std::unique_ptr<juce::AudioIODevice> device_;
    AudioStreamFormat format_;
    std::atomic<IAudioRenderCallback*> target_{};
    std::atomic<bool> running_{}, fault_{}, stopping_{true};
};

static_assert(std::atomic<IAudioRenderCallback*>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<std::uint64_t> nextInstance{1};

juce::var object()
{
    return juce::var(new juce::DynamicObject());
}
juce::var strings(const auto& values)
{
    juce::Array<juce::var> array;
    for (const auto& value : values)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
            array.add(juce::String::fromUTF8(value.c_str()));
        else array.add(juce::var(value));
    }
    return array;
}
juce::var encodeDevice(const DiscoveredAudioDevice& device)
{
    auto value = object();
    auto* data = value.getDynamicObject();
    data->setProperty("key", juce::String::fromUTF8(device.key.c_str()));
    data->setProperty("name", juce::String::fromUTF8(device.name.c_str()));
    data->setProperty("direction", static_cast<int>(device.direction));
    data->setProperty("default", device.systemDefault);
    return value;
}
DiscoveredAudioDevice decodeDevice(const juce::var& value)
{
    return {value["key"].toString().toStdString(), value["name"].toString().toStdString(),
            static_cast<AudioDirection>(static_cast<int>(value["direction"])), static_cast<bool>(value["default"])};
}
AudioError helperError(const juce::var& value)
{
    if (!value.isObject() || !value["code"].isInt() || !value["message"].isString())
        return failure(AudioErrorCode::unavailable, "Audio helper returned an invalid response. Refresh / Retry.");
    const int code = static_cast<int>(value["code"]);
    if (code < 0 || code > static_cast<int>(AudioErrorCode::invalidArgument))
        return failure(AudioErrorCode::unavailable, "Audio helper returned an unknown status. Refresh / Retry.");
    return {static_cast<AudioErrorCode>(code), value["message"].toString().toStdString()};
}
void decodeStrings(const juce::var& values, std::vector<std::string>& result)
{
    if (const auto* array = values.getArray())
        for (const auto& value : *array) result.push_back(value.toString().toStdString());
}

/** Only IPC/process APIs run here. Vendor calls stay on a separate process's message thread. */
struct HelperJob
{
    std::atomic<bool> cancelled{}, done{};
    juce::var response;
    std::uint64_t generation{};
    bool scan{};
    AudioDeviceToken token;
    IAudioBackend::CapabilityCallback capability;
    IAudioBackend::ControlPanelCallback panel;
};
std::shared_ptr<HelperJob> launchHelper(juce::var request, bool panel, AudioDiscoveryHelperOptions options, std::thread& worker)
{
    auto job = std::make_shared<HelperJob>();
    worker = std::thread([job, request = std::move(request), panel, options = std::move(options)]
    {
        auto response = object();
        auto* data = response.getDynamicObject();
        data->setProperty("code", static_cast<int>(AudioErrorCode::unavailable));
        data->setProperty("message", "Audio discovery helper is missing or could not start. Repair the desktop bundle and retry.");
        const auto folder = options.workingDirectory.empty()
            ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VisualAudioLab").getChildFile("Discovery")
            : juce::File(juce::String::fromUTF8(options.workingDirectory.c_str()));
        const auto id = juce::Uuid().toString();
        const auto input = folder.getChildFile(id + ".request.json");
        const auto output = folder.getChildFile(id + ".response.json");
        const auto executable = options.executable.empty() ? juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(
#if JUCE_WINDOWS
            "VisualAudioLab.AudioDiscovery.Helper.exe"
#else
            "VisualAudioLab.AudioDiscovery.Helper"
#endif
        ) : juce::File(juce::String::fromUTF8(options.executable.c_str()));
        juce::StringArray arguments{executable.getFullPathName()};
        for (const auto& argument : options.arguments) arguments.add(juce::String::fromUTF8(argument.c_str()));
        arguments.add(input.getFullPathName()); arguments.add(output.getFullPathName());
        juce::ChildProcess child;
        if (!job->cancelled.load() && folder.createDirectory().wasOk()
            && input.replaceWithText(juce::JSON::toString(request, true))
            && child.start(arguments, 0))
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + (panel ? 300000.0 : options.timeoutMilliseconds);
            while (child.isRunning() && !job->cancelled.load() && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep(10);
            if (child.isRunning())
            {
                child.kill();
                child.waitForProcessToFinish(1000);
                data->setProperty("message", panel ? "Driver Control Panel cancelled or timed out; retry when ready."
                    : "Driver inspection timed out after 15 seconds. Check the driver, then Refresh / Retry.");
            }
            else if (output.existsAsFile() && output.getSize() <= 1024 * 1024)
            {
                auto parsed = juce::JSON::parse(output.loadFileAsString());
                if (parsed.isObject()) response = std::move(parsed);
                else data->setProperty("message", "Audio helper failed or returned invalid data. Refresh / Retry.");
            }
            else data->setProperty("message", "Audio helper exited without a result (driver crash or failure). Refresh / Retry.");
        }
        input.deleteFile();
        output.deleteFile();
        job->response = std::move(response);
        job->done.store(true, std::memory_order_release);
    });
    return job;
}
}

std::string runAudioDiscoveryHelper(const std::string& json)
{
    auto response = object();
    auto* data = response.getDynamicObject();
    const auto finish = [&](AudioError error)
    {
        data->setProperty("code", static_cast<int>(error.code));
        data->setProperty("message", juce::String::fromUTF8(error.message.c_str()));
        return juce::JSON::toString(response, true).toStdString();
    };
    const auto request = juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
    if (!request.isObject() || !request["backend"].isInt() || !request["operation"].isString())
        return finish(failure(AudioErrorCode::invalidArgument, "Invalid audio helper request."));
    const int kind = static_cast<int>(request["backend"]);
    const auto operation = request["operation"].toString();
    if (kind < 0 || kind > 4 || (operation != "scan" && operation != "probe" && operation != "panel"))
        return finish(failure(AudioErrorCode::invalidArgument, "Unsupported audio helper request."));
    if (operation != "scan" && (!request["output"].isObject()
        || !request["output"]["name"].isString() || !request["output"]["key"].isString()))
        return finish(failure(AudioErrorCode::invalidArgument, "Missing audio helper endpoint."));
    if (!onOwnerThread()) return finish(failure(AudioErrorCode::wrongThread, "Run the audio helper protocol on its JUCE message thread."));
    NativeDiscovery source(static_cast<AudioBackendKind>(kind));
    const auto scanned = source.scan();
    if (!scanned.value) return finish(scanned.error);
    if (operation == "scan")
    {
        juce::Array<juce::var> devices;
        for (const auto& device : scanned.value->devices) devices.add(encodeDevice(device));
        data->setProperty("devices", devices);
        data->setProperty("notes", strings(scanned.value->notes));
        return finish({});
    }
    const auto resolve = [&](const juce::var& endpoint) -> std::optional<DiscoveredAudioDevice>
    {
        const auto selected = decodeDevice(endpoint);
        const auto& devices = scanned.value->devices;
        const auto found = std::find(devices.begin(), devices.end(), selected);
        return found == devices.end() ? std::nullopt : std::optional(*found);
    };
    auto output = resolve(request["output"]);
    std::optional<DiscoveredAudioDevice> input;
    if (request["input"].isObject()) input = resolve(request["input"]);
    if (!output || (request["input"].isObject() && !input))
        return finish(failure(AudioErrorCode::staleDevice, "Selected endpoint changed before inspection; Refresh / Retry."));
    if (operation == "panel") return finish(source.showControlPanel(*output));
    auto result = static_cast<bool>(request["endpoint"]) ? source.probe(*output) : source.probeConfiguration(input, *output);
    if (!result.value) return finish(result.error);
    const auto& caps = *result.value;
    data->setProperty("inputs", strings(caps.inputChannels));
    data->setProperty("outputs", strings(caps.outputChannels));
    data->setProperty("rates", strings(caps.sampleRates));
    data->setProperty("buffers", strings(caps.bufferFrames));
    if (caps.defaultBufferFrames) data->setProperty("defaultBuffer", *caps.defaultBufferFrames);
    data->setProperty("notes", strings(caps.notes));
    data->setProperty("panel", caps.hasControlPanel);
    return finish({});
}

/** Serializes discovery and notification phases without native callbacks entering client code. */
class JuceAudioBackend::Impl final : private juce::AsyncUpdater, private juce::Timer
{
public:
    Impl(AudioBackendKind kind, std::unique_ptr<IAudioDeviceDiscovery> source, AudioDiscoveryHelperOptions helper)
        : kind_(kind), instance_(nextInstance.fetch_add(1)), source_(std::move(source)), helperOptions_(std::move(helper))
    {
        jassert(onOwnerThread());
        native_ = !source_ || !helperOptions_.executable.empty();
        if (!source_) source_ = std::make_unique<NativeDiscovery>(kind, true);
        state_.backend = kind;
        source_->setChanged([this]
        {
            invalidated_.store(true);
            triggerAsyncUpdate();
        });
        startTimer(50);
    }
    ~Impl() override
    {
        jassert(onOwnerThread());
        stop();
    }

    AudioError check() const
    {
        if (!onOwnerThread()) return failure(AudioErrorCode::wrongThread, "Audio discovery must run on its JUCE message/control thread.");
        if (stopped_) return failure(AudioErrorCode::stopped, "Audio backend has stopped; create a new backend to restart.");
        return {};
    }

    AudioError stop()
    {
        if (!onOwnerThread()) return failure(AudioErrorCode::wrongThread, "Destroy/shut down the backend on its message/control thread.");
        if (stopped_) return {};
        stopped_ = true;
        if (helper_)
        {
            helper_->capability = {};
            helper_->panel = {};
            helper_->cancelled.store(true);
            if (helperWorker_.joinable()) helperWorker_.join();
            helper_.reset();
        }
        stopTimer();
        source_->stop();
        cancelPendingUpdate();
        requests_.clear();
        completed_.clear();
        completedPanels_.clear();
        listener_ = {};
        pendingNotification_ = false;
        return {};
    }

    AudioError refresh()
    {
        if (const auto error = check(); !error.succeeded()) return error;
        refresh_ = true;
        forceRefresh_ = true;
        triggerAsyncUpdate();
        return {};
    }

    AudioError query(AudioDeviceToken token, CapabilityCallback callback, std::optional<AudioDeviceToken> input = {}, bool configuration = false)
    {
        if (const auto error = check(); !error.succeeded()) return error;
        if (!callback) return failure(AudioErrorCode::invalidArgument, "A capability completion callback is required.");
        if (requests_.size() + completed_.size() + (helper_ ? 1 : 0) >= 32)
            return failure(AudioErrorCode::queueFull, "Audio capability request queue is full; wait for pending queries.");
        requests_.push_back({std::move(token), std::move(callback), std::move(input), configuration});
        triggerAsyncUpdate();
        return {};
    }

    AudioQuery<AudioDeviceCapabilities> probe(const AudioDeviceToken& token, const std::optional<AudioDeviceToken>& input, bool configuration)
    {
        if (source_->diagnostics().open)
            return {{}, failure(AudioErrorCode::invalidArgument, "Close the stream before probing capabilities; probing may initialize another driver instance.")};
        if (token.backend != kind_ || token.instance != instance_ || token.generation != state_.generation
            || invalidated_.load())
            return {{}, failure(AudioErrorCode::staleDevice, "Device token is stale or belongs to another backend. Refresh and select again.")};
        if (!state_.error.succeeded()) return {{}, state_.error};
        const auto found = std::find_if(inventory_.devices.begin(), inventory_.devices.end(),
                                       [&](const auto& device) { return device.key == token.key; });
        if (found == inventory_.devices.end())
            return {{}, failure(AudioErrorCode::staleDevice, "The selected endpoint no longer exists.")};
        std::optional<DiscoveredAudioDevice> inputDevice;
        if (input)
        {
            if (input->backend != kind_ || input->instance != instance_ || input->generation != state_.generation)
                return {{}, failure(AudioErrorCode::staleDevice, "Input device token is stale. Scan and inspect again.")};
            const auto selected = std::find_if(inventory_.devices.begin(), inventory_.devices.end(),
                                              [&](const auto& item) { return item.key == input->key; });
            if (selected == inventory_.devices.end() || selected->direction == AudioDirection::output
                || found->direction == AudioDirection::input)
                return {{}, failure(AudioErrorCode::invalidArgument, "Select valid input/output endpoints.")};
            if (kind_ == AudioBackendKind::asio && selected->key != found->key)
                return {{}, failure(AudioErrorCode::invalidArgument, "ASIO uses one driver for both directions.")};
            inputDevice = *selected;
        }
        if (native_)
        {
            auto request = object();
            auto* data = request.getDynamicObject();
            data->setProperty("backend", static_cast<int>(kind_));
            data->setProperty("operation", "probe");
            data->setProperty("endpoint", !configuration);
            data->setProperty("output", encodeDevice(*found));
            if (inputDevice) data->setProperty("input", encodeDevice(*inputDevice));
            helper_ = launchHelper(std::move(request), false, helperOptions_, helperWorker_);
            helper_->generation = state_.generation;
            return {};
        }
        auto result = configuration ? source_->probeConfiguration(inputDevice, *found) : source_->probe(*found);
        if (invalidated_.load())
            return {{}, failure(AudioErrorCode::staleDevice, "The device changed during its capability query. Refresh and retry.")};
        return result;
    }

    void scan(bool force)
    {
        if (native_)
        {
            auto request = object();
            request.getDynamicObject()->setProperty("backend", static_cast<int>(kind_));
            request.getDynamicObject()->setProperty("operation", "scan");
            helper_ = launchHelper(std::move(request), false, helperOptions_, helperWorker_);
            helper_->scan = true;
            helperForce_ = force;
            return;
        }
        acceptInventory(source_->scan(), force);
    }
    void acceptInventory(const AudioQuery<AudioDiscoveryInventory>& found, bool force)
    {
        const auto nextError = found.value ? AudioError{} : found.error;
        const bool unchanged = (found.value && *found.value == inventory_ && state_.error.succeeded())
            || (!found.value && nextError.code == state_.error.code && nextError.message == state_.error.message);
        if (!force && unchanged) return;
        if (!unchanged && source_->diagnostics().open)
        {
            source_->interruptStream(AudioStreamStop::deviceLost);
            streamStarted_ = false;
        }
        ++state_.generation;
        state_.error = nextError;
        state_.devices.clear();
        inventory_ = found.value.value_or(AudioDiscoveryInventory{});
        state_.notes = inventory_.notes;
        std::set<std::string> keys;
        for (const auto& device : inventory_.devices)
        {
            if (device.key.empty() || !keys.insert(device.key).second)
            {
                state_.error = failure(AudioErrorCode::unavailable, "Discovery returned ambiguous endpoint identities; no devices can safely be selected.");
                state_.devices.clear();
                break;
            }
            state_.devices.push_back({{kind_, instance_, state_.generation, device.key},
                                       device.name, device.direction, device.systemDefault});
        }
        pendingNotification_ = true;
    }

    AudioQuery<AudioStreamFormat> open(const AudioStreamRequest& request)
    {
        if (const auto error = check(); !error.succeeded()) return {{}, error};
        if (source_->diagnostics().open) return {{}, failure(AudioErrorCode::invalidArgument, "Close the current stream before opening another.")};
        if (!state_.error.succeeded()) return {{}, state_.error};
        if (helper_ || invalidated_.load() || refresh_ || !std::isfinite(request.sampleRate)
            || request.sampleRate < 8000 || request.sampleRate > 384000 || request.bufferFrames < 1
            || request.maxFrames < request.bufferFrames || request.maxFrames > 4096
            || request.inputChannels < 0 || request.inputChannels > 32
            || request.outputChannels < 1 || request.outputChannels > 32
            || !validAudioChannelSelection(request.inputChannelIndices, request.inputChannels)
            || !validAudioChannelSelection(request.outputChannelIndices, request.outputChannels)
            || (request.inputChannels > 0) != request.input.has_value() || !request.output)
            return {{}, failure(AudioErrorCode::invalidArgument, "Refresh first and explicitly choose output/input tokens, 8-384 kHz, 1-4096 frame capacity and up to 32 channels.")};
        std::optional<DiscoveredAudioDevice> input, output;
        const auto resolve = [&](const AudioDeviceToken& token, bool isInput) -> AudioError
        {
            if (token.backend != kind_ || token.instance != instance_ || token.generation != state_.generation)
                return failure(AudioErrorCode::staleDevice, "Stream endpoint token is stale or belongs to another backend.");
            const auto found = std::find_if(inventory_.devices.begin(), inventory_.devices.end(),
                [&](const auto& item) { return item.key == token.key; });
            if (found == inventory_.devices.end() || (isInput && found->direction == AudioDirection::output)
                || (!isInput && found->direction == AudioDirection::input))
                return failure(AudioErrorCode::invalidArgument, "Stream endpoint direction/identity is invalid.");
            (isInput ? input : output) = *found;
            return {};
        };
        if (request.input)
            if (const auto error = resolve(*request.input, true); !error.succeeded()) return {{}, error};
        if (const auto error = resolve(*request.output, false); !error.succeeded()) return {{}, error};
        if (kind_ == AudioBackendKind::asio && input && output && input->key != output->key)
            return {{}, failure(AudioErrorCode::invalidArgument, "ASIO input and output must use the same driver.")};
        auto opened = source_->open(input, output, request);
        if (invalidated_.load())
        {
            source_->interruptStream(AudioStreamStop::deviceLost);
            return {{}, failure(AudioErrorCode::staleDevice, "Device changed while opening; refresh and retry.")};
        }
        if (opened.value)
        {
            const auto& format = *opened.value;
            if (!std::isfinite(format.sampleRate) || format.sampleRate != request.sampleRate
                || format.bufferFrames != request.bufferFrames || format.bufferFrames > format.maxFrames
                || format.maxFrames != request.maxFrames || format.inputChannels != request.inputChannels
                || format.outputChannels != request.outputChannels)
            {
                source_->closeStream();
                return {{}, failure(AudioErrorCode::unavailable, "Driver changed the requested rate, buffer or channels. Stop, inspect again or configure the vendor/OS audio panel; no silent substitution is accepted.")};
            }
        }
        return opened;
    }
    AudioError showPanel(AudioDeviceToken token, ControlPanelCallback callback)
    {
        if (const auto error = check(); !error.succeeded()) return error;
        if (!callback) return failure(AudioErrorCode::invalidArgument, "A Control Panel completion is required.");
        if (helper_ || !requests_.empty() || source_->diagnostics().open)
            return failure(AudioErrorCode::invalidArgument, "Stop audio and wait for inspection before opening Control Panel.");
        if (invalidated_.load() || token.backend != kind_ || token.instance != instance_ || token.generation != state_.generation)
            return failure(AudioErrorCode::staleDevice, "Driver identity changed; refresh and select again.");
        const auto found = std::find_if(inventory_.devices.begin(), inventory_.devices.end(),
            [&](const auto& item) { return item.key == token.key && item.direction != AudioDirection::input; });
        if (found == inventory_.devices.end()) return failure(AudioErrorCode::staleDevice, "Selected output driver no longer exists.");
        if (native_)
        {
            auto request = object();
            auto* data = request.getDynamicObject();
            data->setProperty("backend", static_cast<int>(kind_));
            data->setProperty("operation", "panel");
            data->setProperty("output", encodeDevice(*found));
            helper_ = launchHelper(std::move(request), true, helperOptions_, helperWorker_);
            helper_->panel = std::move(callback);
        }
        else completedPanels_.push_back({std::move(callback), source_->showControlPanel(*found)});
        triggerAsyncUpdate();
        return {};
    }

    void handleAsyncUpdate() override
    {
        if (stopped_) return;
        if (helper_)
        {
            if (!helper_->done.load(std::memory_order_acquire)) return;
            if (helperWorker_.joinable()) helperWorker_.join();
            auto helper = std::exchange(helper_, {});
            auto error = helperError(helper->response);
            if (error.succeeded() && (helper->scan ? !helper->response["devices"].isArray()
                : !helper->panel && (!helper->response["inputs"].isArray() || !helper->response["outputs"].isArray()
                    || !helper->response["rates"].isArray() || !helper->response["buffers"].isArray()
                    || !helper->response["panel"].isBool())))
                error = failure(AudioErrorCode::unavailable, "Audio helper returned incomplete data. Refresh / Retry.");
            if (helper->scan)
            {
                AudioQuery<AudioDiscoveryInventory> result{{}, error};
                if (error.succeeded())
                {
                    result.value.emplace();
                    if (const auto* array = helper->response["devices"].getArray())
                        for (const auto& value : *array) result.value->devices.push_back(decodeDevice(value));
                    decodeStrings(helper->response["notes"], result.value->notes);
                }
                acceptInventory(result, helperForce_);
            }
            else if (helper->panel)
            {
                completedPanels_.push_back({std::move(helper->panel), error});
            }
            else
            {
                AudioQuery<AudioDeviceCapabilities> result{{}, error};
                if (error.succeeded())
                {
                    result.value.emplace();
                    auto& caps = *result.value;
                    decodeStrings(helper->response["inputs"], caps.inputChannels);
                    decodeStrings(helper->response["outputs"], caps.outputChannels);
                    decodeStrings(helper->response["notes"], caps.notes);
                    if (const auto* values = helper->response["rates"].getArray())
                        for (const auto& value : *values) caps.sampleRates.push_back(static_cast<double>(value));
                    if (const auto* values = helper->response["buffers"].getArray())
                        for (const auto& value : *values) caps.bufferFrames.push_back(static_cast<int>(value));
                    if (helper->response["defaultBuffer"].isInt()) caps.defaultBufferFrames = static_cast<int>(helper->response["defaultBuffer"]);
                    caps.hasControlPanel = static_cast<bool>(helper->response["panel"]);
                }
                completed_.push_back({std::move(helper->token), std::move(helper->capability), std::move(result)});
            }
            triggerAsyncUpdate();
            return;
        }
        const bool invalidated = invalidated_.exchange(false);
        const auto refresh = std::exchange(refresh_, false);
        const auto poll = std::exchange(poll_, false);
        if (refresh || poll || invalidated)
        {
            const auto stream = source_->diagnostics();
            if (stream.open && (invalidated || (!stream.running && streamStarted_)))
            {
                source_->interruptStream(invalidated ? AudioStreamStop::deviceLost : AudioStreamStop::deviceError);
                streamStarted_ = false;
            }
            const auto force = std::exchange(forceRefresh_, false);
            scan(invalidated || force);
            triggerAsyncUpdate();
            return;
        }
        // Client callbacks run in a separate dispatch phase from driver calls.
        if (pendingNotification_ || !completed_.empty() || !completedPanels_.empty())
        {
            auto panels = std::move(completedPanels_);
            completedPanels_.clear();
            auto completed = std::move(completed_);
            completed_.clear();
            const auto notify = std::exchange(pendingNotification_, false);
            const auto snapshot = state_;
            const auto listener = listener_;
            const std::weak_ptr<int> alive = lifetime_;
            if (notify && listener) listener(snapshot);
            if (alive.expired()) return;
            if (stopped_) return;
            for (auto& [callback, error] : panels)
            {
                callback(std::move(error));
                if (alive.expired() || stopped_) return;
            }
            for (auto& result : completed)
            {
                if (result.token.generation != state_.generation || invalidated_.load())
                    result.result = {{}, failure(AudioErrorCode::staleDevice, "Inventory changed before capability delivery. Refresh and retry.")};
                result.callback(std::move(result.result));
                if (alive.expired()) return;
                if (stopped_) return;
            }
        }
        else if (!requests_.empty())
        {
            auto request = std::move(requests_.front());
            requests_.pop_front();
            auto result = probe(request.token, request.input, request.configuration);
            if (helper_)
            {
                helper_->token = std::move(request.token);
                helper_->capability = std::move(request.callback);
            }
            else completed_.push_back({std::move(request.token), std::move(request.callback), std::move(result)});
        }
        if (pendingNotification_ || !completed_.empty() || !requests_.empty() || invalidated_.load() || refresh_ || poll_)
            triggerAsyncUpdate();
    }
    void timerCallback() override
    {
        if (helper_) { triggerAsyncUpdate(); return; }
        if (++pollTicks_ >= 40) { pollTicks_ = 0; poll_ = true; triggerAsyncUpdate(); }
    }

    AudioBackendKind kind_;
    std::uint64_t instance_;
    std::unique_ptr<IAudioDeviceDiscovery> source_;
    AudioDiscoveryHelperOptions helperOptions_;
    bool native_{}, helperForce_{};
    int pollTicks_{};
    std::shared_ptr<HelperJob> helper_;
    std::thread helperWorker_;
    std::vector<std::pair<ControlPanelCallback, AudioError>> completedPanels_;
    AudioBackendSnapshot state_;
    AudioDiscoveryInventory inventory_;
    ChangeCallback listener_;
    /** One queued, generation-checked control operation. */
    struct Request { AudioDeviceToken token; CapabilityCallback callback; std::optional<AudioDeviceToken> input; bool configuration{}; };
    /** Driver result waiting for a later client-notification phase. */
    struct Completion { AudioDeviceToken token; CapabilityCallback callback; AudioQuery<AudioDeviceCapabilities> result; };
    std::deque<Request> requests_;
    std::deque<Completion> completed_;
    std::atomic<bool> invalidated_{false};
    bool stopped_{}, refresh_{}, poll_{}, forceRefresh_{}, pendingNotification_{};
    bool streamStarted_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

JuceAudioBackend::JuceAudioBackend(AudioBackendKind kind, std::unique_ptr<IAudioDeviceDiscovery> source, AudioDiscoveryHelperOptions helper)
    : impl_(std::make_unique<Impl>(kind, std::move(source), std::move(helper))) {}
JuceAudioBackend::~JuceAudioBackend() = default;
AudioBackendKind JuceAudioBackend::kind() const noexcept { return impl_->kind_; }
AudioQuery<AudioBackendSnapshot> JuceAudioBackend::snapshot() const
{
    if (const auto error = impl_->check(); !error.succeeded()) return {{}, error};
    return {impl_->state_, {}};
}
AudioError JuceAudioBackend::setChangeCallback(ChangeCallback callback)
{
    if (const auto error = impl_->check(); !error.succeeded()) return error;
    impl_->listener_ = std::move(callback);
    impl_->pendingNotification_ = false;
    return {};
}
AudioError JuceAudioBackend::refresh()
{
    return impl_->refresh();
}
AudioError JuceAudioBackend::queryCapabilities(AudioDeviceToken token, CapabilityCallback callback)
{ return impl_->query(std::move(token), std::move(callback)); }
AudioError JuceAudioBackend::queryConfigurationCapabilities(std::optional<AudioDeviceToken> input,
    AudioDeviceToken output, CapabilityCallback callback)
{ return impl_->query(std::move(output), std::move(callback), std::move(input), true); }
AudioError JuceAudioBackend::shutdown() { return impl_->stop(); }
AudioError JuceAudioBackend::showControlPanel(AudioDeviceToken token, ControlPanelCallback callback)
{ return impl_->showPanel(std::move(token), std::move(callback)); }
AudioQuery<AudioStreamFormat> JuceAudioBackend::openStream(const AudioStreamRequest& request) { return impl_->open(request); }
AudioError JuceAudioBackend::startStream(IAudioRenderCallback& target)
{
    if (const auto error = impl_->check(); !error.succeeded()) return error;
    if (impl_->invalidated_.load() || impl_->refresh_)
        return failure(AudioErrorCode::staleDevice, "Device inventory changed before start; close, refresh and reselect.");
    const auto result = impl_->source_->start(target);
    impl_->streamStarted_ = result.succeeded();
    return result;
}
AudioError JuceAudioBackend::stopStream()
{
    if (const auto error = impl_->check(); !error.succeeded()) return error;
    impl_->source_->stopStream(); impl_->streamStarted_ = false;
    return {};
}
AudioError JuceAudioBackend::closeStream()
{
    if (const auto error = impl_->check(); !error.succeeded()) return error;
    impl_->source_->closeStream(); impl_->streamStarted_ = false;
    return {};
}
AudioQuery<AudioStreamDiagnostics> JuceAudioBackend::streamDiagnostics() const
{
    if (const auto error = impl_->check(); !error.succeeded()) return {{}, error};
    return {impl_->source_->diagnostics(), {}};
}

AsioBackend::AsioBackend(std::unique_ptr<IAudioDeviceDiscovery> source)
    : JuceAudioBackend(AudioBackendKind::asio, std::move(source)) {}
WasapiExclusiveBackend::WasapiExclusiveBackend(std::unique_ptr<IAudioDeviceDiscovery> source)
    : JuceAudioBackend(AudioBackendKind::wasapiExclusive, std::move(source)) {}
WasapiSharedBackend::WasapiSharedBackend(std::unique_ptr<IAudioDeviceDiscovery> source, AudioDiscoveryHelperOptions helper)
    : JuceAudioBackend(AudioBackendKind::wasapiShared, std::move(source), std::move(helper)) {}
CoreAudioBackend::CoreAudioBackend(std::unique_ptr<IAudioDeviceDiscovery> source)
    : JuceAudioBackend(AudioBackendKind::coreAudio, std::move(source)) {}
DirectSoundBackend::DirectSoundBackend(std::unique_ptr<IAudioDeviceDiscovery> source)
    : JuceAudioBackend(AudioBackendKind::directSound, std::move(source)) {}

std::unique_ptr<IAudioBackend> createPreferredAudioBackend()
{
#if JUCE_WINDOWS
    return std::make_unique<AsioBackend>();
#else
    return std::make_unique<CoreAudioBackend>();
#endif
}
}
