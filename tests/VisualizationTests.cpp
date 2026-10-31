#include "TestSupport.h"
#include "AudioAllocationProbe.h"
#include <val/audio/AnalyzerEngine.h>
#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <thread>

using namespace val::audio;

namespace
{
/** Deterministic off-device transport/worker harness; no backend/device is constructed. */
struct AnalysisFixture
{
    AnalyzerEngine engine{false};
    std::shared_ptr<SignalCaptureBuffer> capture;
    SignalAnalysis result;
    std::uint64_t position{};
    double rate;
    explicit AnalysisFixture(double sampleRate = 48000, int channels = 1, AnalyzerProfile profile = {}) : rate(sampleRate)
    {
        capture = std::make_shared<SignalCaptureBuffer>(7, rate, channels);
        require(engine.add(capture, profile).succeeded, "Admit calibrated test source.");
        require(engine.demand(7, true).succeeded, "Explicitly arm observation.");
    }
    template <typename Signal>
    void feed(int frames, Signal signal, int block = 256, std::uint64_t revision = 1, std::uint64_t epoch = 1)
    {
        std::array<float, 4096 * 2> data{};
        while (frames)
        {
            const auto count = std::min(frames, block);
            for (int channel = 0; channel < capture->channels(); ++channel)
                for (int i = 0; i < count; ++i) data[channel * block + i] = signal(position + i, channel);
            {
                allocationProbe::Scope probe;
                capture->capture(data.data(), block, count, revision, epoch, position);
                require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0,
                        "Only wait-free preallocated copy/publication occurs on the capture callback.");
            }
            position += count; frames -= count;
            while (engine.pump()) {}
            require(engine.latest(7, result), "Ingested frames yield a coherent UI-owned result copy.");
        }
    }
};
bool near(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}
void calibration()
{
    for (double rate : {44100.0, 48000.0, 96000.0})
    {
        for (int bin : {4, 43, 311})
        {
            AnalyzerProfile profile; profile.spectrumAverageSeconds = 0;
            AnalysisFixture fixture(rate, 2, profile);
            const auto frequency = rate * bin / profile.fftSize;
            fixture.feed(static_cast<int>(rate * 0.4), [=](std::uint64_t frame, int channel)
            {
                return static_cast<float>((channel ? 0.5 : 1.0) * std::sin(2 * std::numbers::pi * frequency * static_cast<double>(frame) / rate));
            }, 512);
            const auto& data = fixture.result;
            require(data.rmsValid && data.spectrumValid && data.peakValid, "Complete windows have explicit validity.");
            require(near(AnalyzerEngine::decibels(data.rms[0]), -3.0103, 0.035)
                    && near(AnalyzerEngine::decibels(data.rms[1]), -9.0309, 0.035), "Raw sine RMS is calibrated without an AES17 offset.");
            require(near(data.amplitudeDb[0][bin], 0, 0.002) && near(data.amplitudeDb[1][bin], -6.0206, 0.002),
                    "Hann coherent-gain-corrected full-scale and half-scale bin-centred sines read calibrated dBFS.");
            require(near(data.dominantHz[0], frequency, 0.00001) && near(data.coherentGain, 0.5, 0.000001)
                    && near(data.enbwBins, 1.5, 0.000001), "Bin frequency, periodic Hann gain and ENBW are explicit.");
            double integratedPower = 0;
            for (int i = 0; i <= data.fftSize / 2; ++i)
                integratedPower += std::pow(10.0, data.densityDb[0][i] / 10.0) * data.binHz;
            require(near(integratedPower, 0.5, 0.00001), "One-sided PSD integrates to sine power with window-energy/ENBW correction.");
            require(data.peak[0] <= 1 && data.peak[0] > 0.999f, "Sample peak convention amplitude one equals zero dBFS.");
        }
        AnalysisFixture dc(rate);
        dc.feed(static_cast<int>(rate * 0.4), [](auto, int) { return 0.5f; });
        require(near(dc.result.rms[0], 0.5, 0.000001) && near(dc.result.mean[0], 0.5, 0.000001)
                && near(dc.result.amplitudeDb[0][0], -6.0206, 0.001), "DC uses undoubled FFT endpoint, raw RMS and signed mean.");
        AnalysisFixture silence(rate);
        silence.feed(static_cast<int>(rate * 0.4), [](auto, int) { return 0.0f; });
        require(silence.result.rmsValid && silence.result.rms[0] == 0 && silence.result.peak[0] == 0
                && std::isinf(silence.result.amplitudeDb[0][0]) && AnalyzerEngine::decibels(0) < -1000,
                "Measured silence is valid negative-infinite dBFS, not unavailable data.");
        AnalysisFixture square(rate);
        square.feed(static_cast<int>(rate * 0.4), [](auto frame, int) { return frame % 64 < 32 ? 1.0f : -1.0f; });
        require(near(square.result.rms[0], 1, 0.000001) && square.result.peak[0] == 1
                && square.result.overRange[0] > 0, "Full-scale square raw RMS/peak both read 0 dBFS; clipping count is sample-based.");
        AnalysisFixture impulse(rate);
        impulse.feed(static_cast<int>(rate * 0.4), [](auto, int) { return 0.0f; });
        impulse.feed(256, [](auto frame, int) { return frame % 256 == 0 ? 1.0f : 0.0f; });
        require(impulse.result.peak[0] == 1 && near(impulse.result.rms[0], 1.0 / std::sqrt(impulse.result.rmsFrames), 0.000001),
                "Single impulse raw RMS reflects the full 300 ms sliding coverage.");
        bool envelopeContainsImpulse = false;
        for (const auto value : impulse.result.scopeMaximum[0]) envelopeContainsImpulse |= value == 1;
        require(envelopeContainsImpulse, "Min/max scope envelopes retain impulses instead of decimating them away.");
        impulse.feed(static_cast<int>(rate * 0.12), [](auto, int) { return 0.0f; });
        require(impulse.result.peak[0] == 0 && impulse.result.heldPeak[0] == 1
                && impulse.result.displayedPeak[0] < 1, "Peak window, one-second hold and time-based release are separate.");
        impulse.feed(static_cast<int>(rate * 1.2), [](auto, int) { return 0.0f; });
        require(impulse.result.heldPeak[0] < 1 && impulse.result.rms[0] == 0, "Hold eventually decays and rolling RMS clears.");
    }
    AnalyzerProfile trigger; trigger.trigger = AnalyzerProfile::Trigger::rising;
    AnalysisFixture triggered(48000, 1, trigger);
    triggered.feed(24000, [](auto frame, int) { return static_cast<float>(std::sin(2 * std::numbers::pi * 375 * frame / 48000)); });
    require(triggered.result.triggered && triggered.result.scopeFrames == trigger.preTriggerFrames + 512,
            "Rising trigger retains configured pretrigger context and bounded posttrigger span.");
    AnalysisFixture offBin;
    constexpr double frequency = 1000;
    offBin.feed(24000, [](auto frame, int) { return static_cast<float>(std::sin(2 * std::numbers::pi * frequency * frame / 48000)); });
    require(std::abs(offBin.result.dominantHz[0] - frequency) <= offBin.result.binHz / 2,
            "Off-bin dominant-bin error is bounded by half a bin, not labelled a precise fundamental.");
    for (int size : {1024, 8192})
    {
        AnalyzerProfile profile; profile.fftSize = size; profile.spectrumAverageSeconds = 0;
        AnalysisFixture boundary(48000, 1, profile);
        boundary.feed(24000, [size](auto frame, int)
        { return static_cast<float>(std::sin(2 * std::numbers::pi * 71 * static_cast<double>(frame) / size)); }, 1024);
        require(near(boundary.result.amplitudeDb[0][71], 0, 0.002), "All admitted FFT boundary sizes retain Hann calibration.");
    }
    AnalysisFixture longBlock(8000), shortBlock(8000);
    auto oneImpulse = [](auto frame, int) { return frame == 0 ? 1.0f : 0.0f; };
    longBlock.feed(4096, oneImpulse, 4096);
    shortBlock.feed(4096, oneImpulse, 64);
    require(longBlock.result.heldPeak[0] == 1 && longBlock.result.peak[0] == 0
            && near(longBlock.result.displayedPeak[0], shortBlock.result.displayedPeak[0], 0.000001),
            "Hold/ballistics inspect every worker-side sample, retaining impulses even when a host packet exceeds the peak window.");
}
void continuityAndAdmission()
{
    AnalysisFixture fixture;
    fixture.feed(24000, [](auto, int) { return 0.25f; });
    const auto segment = fixture.result.segment;
    fixture.position += 512;
    fixture.feed(256, [](auto, int) { return 0.5f; });
    require(fixture.result.segment > segment && fixture.result.gap && !fixture.result.rmsValid && !fixture.result.spectrumValid,
            "Discontinuity invalidates partial FFT/RMS and restarts a continuity segment.");
    fixture.feed(256, [](auto, int) { return 0.5f; }, 256, 2, 2);
    require(fixture.result.stamp.epoch == 2 && fixture.result.stamp.revision == 2 && fixture.result.gap,
            "Epoch/revision changes cannot blend graphs or stream timelines.");
    fixture.feed(256, [](auto frame, int) { return frame % 256 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.0f; }, 256, 2, 2);
    require(fixture.result.nonFinite && fixture.result.invalidSamples == 1 && !fixture.result.rmsValid,
            "Invalid samples invalidate analysis rather than fabricating calibrated zero.");
    fixture.feed(24000, [](auto, int) { return 0.5f; }, 256, 2, 2);
    require(fixture.result.rmsValid && near(fixture.result.rms[0], 0.5, 0.000001), "Finite contiguous data recovers after invalidation.");
    fixture.feed(4096, [](auto frame, int) { return frame % 2 ? std::numeric_limits<float>::infinity() : 1.0f; }, 4096, 2, 2);
    require(fixture.result.invalidSamples == 2049 && !fixture.result.rmsValid && !fixture.result.spectrumValid,
            "Repeated invalidation stays bounded without clearing whole history/FFT storage on every sample.");
    std::array<float, 512> input{}; input.fill(0.75f);
    auto ring = std::make_shared<SignalCaptureBuffer>(1, 48000, 1);
    ring->setEnabled(true);
    for (int i = 0; i < 10; ++i) ring->capture(input.data(), 512, 512, 1, 1, i * 512);
    require(ring->drops() == 6 && ring->capturedFrames() == 2048, "Four owned slots drop incoming, never overwrite unread data.");
    const auto* oldest = ring->acquire();
    require(oldest && oldest->stamp.firstFrame == 0 && oldest->samples[0] == 0.75f, "Producer never advances consumer ownership on overflow.");
    ring->release();
    ring->setEnabled(false);
    ring->capture(input.data(), 512, 512, 1, 1, 5000);
    require(ring->capturedFrames() == 2048, "No graphical demand means no capture sample work.");
    AnalyzerEngine engine(false);
    require(engine.add(ring).succeeded && !engine.add(ring).succeeded, "Identical source must share one analysis path.");
    AnalyzerProfile invalid; invalid.fftSize = 1000;
    require(!engine.add(std::make_shared<SignalCaptureBuffer>(2, 48000, 1), invalid).succeeded, "Reject invalid FFT rather than silently coercing.");
    for (std::uint64_t i = 2; i <= 8; ++i)
        require(engine.add(std::make_shared<SignalCaptureBuffer>(i, 48000, 2)).succeeded, "Admit bounded shared source set.");
    require(!engine.add(std::make_shared<SignalCaptureBuffer>(9, 48000, 1)).succeeded, "Unique overload explicitly refuses ninth source.");
    require(engine.diagnostics().memoryBytes < 64 * 1024 * 1024, "Actual observation storage is bounded below 64 MiB.");
    for (int i = 0; i < 108; ++i) require(engine.demand(1, true).succeeded, "108 compatible hosts share one capture.");
    require(!engine.demand(1, true).succeeded, "Host admission cannot grow without bound.");
    for (int i = 0; i < 108; ++i) require(engine.demand(1, false).succeeded, "Detach balanced demands.");
    require(!ring->enabled() && !engine.demand(1, false).succeeded, "Last host disables capture; mismatched demand is explicit.");
    engine.remove(1);
    require(ring->retired() && !engine.add(ring).succeeded, "Removed generations cannot replay queued samples in a new observation session.");
    ring->setEnabled(true);
    require(!ring->enabled(), "Retirement cannot be reversed while old prepared graphs still own the transport.");
    AnalyzerEngine bandwidth(false);
    require(bandwidth.add(std::make_shared<SignalCaptureBuffer>(1, 384000, 2)).succeeded
            && !bandwidth.add(std::make_shared<SignalCaptureBuffer>(2, 8000, 1)).succeeded, "Higher rates obey the explicit 3.072 MB/s copy ceiling.");
    auto wide = std::make_shared<SignalCaptureBuffer>(10, 48000, 2, 2);
    wide->setEnabled(true);
    std::vector<float> planar(4096 * 4);
    std::fill(planar.begin() + 8192, planar.begin() + 12288, 0.25f);
    std::fill(planar.begin() + 12288, planar.end(), 0.75f);
    wide->capture(planar.data(), 4096, 4096, 7, 9, 1000);
    const auto* first = wide->acquire();
    require(first && first->stamp.frames == 2048 && first->samples[0] == 0.25f && first->samples[2048] == 0.75f,
            "Selected channels on wider buses preserve layout and bounded two-slot segmentation.");
    wide->release();
    require(wide->acquire()->stamp.firstFrame == 3048, "Second segment carries the correct absolute sample position.");
    wide->release();
}
void concurrentTransport()
{
    auto ring = std::make_shared<SignalCaptureBuffer>(1, 48000, 2);
    ring->setEnabled(true);
    std::atomic<bool> done{}, corrupted{};
    std::thread producer([&]
    {
        std::array<float, 512> data{};
        for (std::uint64_t i = 0; i < 10000; ++i)
        {
            data.fill(static_cast<float>(i));
            ring->capture(data.data(), 256, 256, 3, 5, i * 256);
        }
        done.store(true, std::memory_order_release);
    });
    std::uint64_t previous = 0;
    while (!done.load(std::memory_order_acquire) || ring->acquire())
    {
        if (const auto* page = ring->acquire())
        {
            const auto value = static_cast<float>(page->stamp.firstFrame / 256);
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < 256; ++frame)
                    if (page->samples[channel * 2048 + frame] != value) corrupted.store(true);
            if (page->stamp.sequence <= previous) corrupted.store(true);
            previous = page->stamp.sequence; ring->release();
        }
        else std::this_thread::yield();
    }
    producer.join();
    require(!corrupted.load(), "Concurrent SPSC payload ownership never tears or overwrites Reading.");
    auto results = std::make_unique<AnalysisResults>();
    done.store(false); corrupted.store(false);
    std::thread worker([&]
    {
        auto value = std::make_unique<SignalAnalysis>();
        for (std::uint64_t i = 1; i <= 10000; ++i)
        {
            value->stamp.sequence = i; value->peak.fill(static_cast<float>(i)); value->rms.fill(static_cast<float>(i));
            results->publish(*value);
        }
        done.store(true, std::memory_order_release);
    });
    auto value = std::make_unique<SignalAnalysis>();
    std::uint64_t version = 0;
    while (!done.load(std::memory_order_acquire))
        if (results->latest(*value))
        {
            if (value->stamp.sequence < version || value->peak[0] != value->rms[1]
                || value->peak[0] != static_cast<float>(value->stamp.sequence)) corrupted.store(true);
            version = value->stamp.sequence;
        }
    worker.join();
    require(!corrupted.load(), "Fixed result pool never overwrites a UI Reading page or tears a transaction.");
}
void graphCapture()
{
    using namespace val::application;
    AudioEngine engine;
    const AudioStreamFormat format{48000, 256, 512, 1, 1};
    AnalysisFixture observation;
    auto graph = AudioGraph{1, {{1, AudioNodeType::input}, {7, AudioNodeType::gain, 0.5f}, {9, AudioNodeType::output}}, {{1, 7}, {7, 9}}};
    graph.nodes[1].capture = observation.capture;
    require(engine.startOffline(format, graph, 1).succeeded, "Attach capture to an already scheduled Gain, not a second analyzer hierarchy.");
    require(engine.scheduleParameter(engine.telemetry().value->epoch, 1, 7, 0, 128, 0).succeeded, "Split a callback for sample-accurate automation.");
    std::array<float, 256> input{}, output{}; input.fill(0.5f);
    const float* inputs[]{input.data()}; float* outputs[]{output.data()};
    {
        allocationProbe::Scope probe;
        engine.process({inputs, outputs, 1, 1, 256});
        require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0, "Integrated engine and tap remain allocation/free-free.");
    }
    const auto* packet = observation.capture->acquire();
    require(packet && packet->stamp.frames == 256 && packet->stamp.firstFrame == 0,
            "Automation subspans yield exactly one complete capture, not fragmented per-event pages.");
    for (int i = 0; i < 256; ++i) require(packet->samples[i] == output[i], "Capture matches actual graph output sample for sample.");
    observation.capture->release();
    graph.nodes[1].capture = std::make_shared<SignalCaptureBuffer>(123, 48000, 1);
    require(!engine.publish(graph, 2).succeeded, "Wrong source identity cannot observe a reused prepared slot.");
    require(engine.publish(AudioGraph{}, 2).succeeded == false, "Channel-layout mismatch remains an error.");
}
void stress()
{
    AnalyzerEngine engine(false);
    std::vector<std::shared_ptr<SignalCaptureBuffer>> sources;
    std::array<float, 512 * 2> samples{};
    for (int i = 0; i < 512; ++i)
    {
        samples[i] = static_cast<float>(std::sin(2 * std::numbers::pi * 937.5 * i / 48000));
        samples[i + 512] = samples[i] * 0.5f;
    }
    for (std::uint64_t id = 1; id <= 8; ++id)
    {
        auto source = std::make_shared<SignalCaptureBuffer>(id, 48000, 2);
        require(engine.add(source).succeeded && engine.demand(id, true).succeeded, "Admit 16-channel many-monitor load.");
        sources.push_back(std::move(source));
    }
    for (int view = 0; view < 100; ++view) require(engine.demand(1 + view % 8, true).succeeded, "100 embedded previews share eight detailed sources.");
    std::vector<double> captureTimes; captureTimes.reserve(2000);
    auto result = std::make_unique<SignalAnalysis>();
    for (int block = 0; block < 2000; ++block)
    {
        const auto began = std::chrono::steady_clock::now();
        {
            allocationProbe::Scope probe;
            for (auto& source : sources) source->capture(samples.data(), 512, 512, 1, 1, static_cast<std::uint64_t>(block) * 512);
            require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0, "16-channel capture stress allocates/frees nothing.");
        }
        captureTimes.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count());
        require(engine.pump() == 8, "One fair source round per audio block.");
        if (block % 3 == 0)
            for (auto& source : sources) { const auto read = engine.latest(source->source(), *result); require(read, "Shared UI result available."); }
    }
    std::sort(captureTimes.begin(), captureTimes.end());
    std::uint64_t drops = 0;
    for (auto& source : sources) drops += source->drops();
    const auto stats = engine.diagnostics();
    std::cout << "SYNTHETIC_OFFLINE sources=8 channels=16 embedded_hosts=100 detailed_hosts=8 block=512 rate=48000 blocks=2000\n"
              << "capture_us p50=" << captureTimes[1000] << " p99=" << captureTimes[1980] << " max=" << captureTimes.back()
              << " p99_callback_percent=" << captureTimes[1980] / (512.0 / 48000 * 1.0e6) * 100 << '\n'
              << "analysis_job_max_ms=" << stats.maxJobMilliseconds << " jobs=" << stats.jobs << " storage_bytes=" << stats.memoryBytes
              << " capture_drops=" << drops << " recycled_UI_results=" << stats.publicationDrops << " callback_allocations=0 callback_frees=0\n";
    require(drops == 0, "Synchronous steady ingestion loses no admitted samples.");
    for (int burst = 0; burst < 20; ++burst)
        for (auto& source : sources) source->capture(samples.data(), 512, 512, 1, 1, 2000000 + burst * 512);
    std::uint64_t overloadedDrops = 0;
    for (auto& source : sources) overloadedDrops += source->drops();
    require(overloadedDrops == 128, "Deliberately stalled consumer drops exactly incoming slots without blocking audio.");
    std::cout << "stalled_worker_20_blocks_capture_drops=" << overloadedDrops << '\n';
}
}
int main(int argc, char** argv)
{
    return runTests([&]
    {
        if (argc == 2 && std::string_view(argv[1]) == "--stress") { stress(); return; }
        calibration(); continuityAndAdmission(); concurrentTransport(); graphCapture();
    });
}
