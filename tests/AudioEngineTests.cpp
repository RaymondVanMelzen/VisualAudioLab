#include "TestSupport.h"
#include "AudioAllocationProbe.h"
#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

namespace
{
using namespace val::audio;
using namespace val::application;

AudioGraph chain(float gain = 0.5f, int gains = 1)
{
    AudioGraph graph;
    graph.nodes.push_back({1, AudioNodeType::input});
    for (int i = 0; i < gains; ++i)
    {
        graph.nodes.push_back({static_cast<std::uint64_t>(i + 2), AudioNodeType::gain, i == 0 ? gain : 1.0f});
        graph.connections.push_back({static_cast<std::uint64_t>(i + 1), static_cast<std::uint64_t>(i + 2)});
    }
    graph.nodes.push_back({static_cast<std::uint64_t>(gains + 2), AudioNodeType::output});
    graph.connections.push_back({static_cast<std::uint64_t>(gains + 1), static_cast<std::uint64_t>(gains + 2)});
    return graph;
}
AudioStreamFormat format() { return {48000, 128, 512, 2, 2}; }

/** Fixed test input/output storage sized beyond the legal block to check guard regions. */
struct Buffers
{
    std::array<float, 1024> left{}, right{}, outLeft{}, outRight{};
    const float* input[2]{left.data(), right.data()};
    float* output[2]{outLeft.data(), outRight.data()};
    Buffers()
    {
        left.fill(0.5f); right.fill(-0.25f);
        outLeft.fill(99); outRight.fill(99);
    }
    AudioBuffer span(int frames) { return {input, output, 2, 2, frames}; }
};

void render(AudioEngine& engine, const AudioBuffer& buffer)
{
    std::uint64_t allocations{}, deallocations{};
    {
        allocationProbe::Scope scope;
        engine.process(buffer);
        allocations = allocationProbe::allocations;
        deallocations = allocationProbe::deallocations;
    }
    require(allocations == 0 && deallocations == 0, "Audio callback allocates/frees no C++ memory, including graph activation.");
}

void testProcessing()
{
    AudioEngine engine;
    Buffers buffers;
    require(engine.startOffline(format(), chain(), 1).succeeded, "Prepare basic Input -> Gain -> Output.");
    for (auto frames : {0, 1, 17, 128, 511, 512})
    {
        buffers.outLeft.fill(99); buffers.outRight.fill(99);
        render(engine, buffers.span(frames));
        for (int i = 0; i < frames; ++i)
            require(buffers.outLeft[i] == 0.25f && buffers.outRight[i] == -0.125f,
                    "Actual multichannel samples traverse the graph.");
        require(buffers.outLeft[frames] == 99 && buffers.outRight[frames] == 99, "Process exactly the borrowed span, not capacity.");
    }
    buffers.input[0] = nullptr;
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0 && buffers.outRight[0] == -0.125f, "Null input channels are silence, not stale scratch.");
    buffers.input[0] = buffers.left.data();
    buffers.output[1] = nullptr;
    render(engine, buffers.span(128));
    buffers.output[1] = buffers.outRight.data();
    buffers.output[0] = buffers.left.data();
    render(engine, buffers.span(128));
    require(buffers.left[0] == 0.25f, "Preallocated input staging permits in-place/aliased host buffers.");
    buffers.left.fill(0.5f);
    buffers.output[0] = buffers.outLeft.data();
    require(!engine.setGain(0, 2, 1).succeeded && !engine.setGain(1, 2, -1).succeeded
            && !engine.setGain(1, 2, std::numeric_limits<float>::quiet_NaN()).succeeded,
            "Invalid revisions/gains do not enter realtime messages.");
    require(engine.setGain(1, 2, 1).succeeded, "Queue live gain without graph reconstruction.");
    render(engine, buffers.span(100));
    const auto first = buffers.outLeft[0], hundredth = buffers.outLeft[99];
    render(engine, buffers.span(140));
    require(first > 0.25f && first < hundredth && hundredth < 0.5f
            && buffers.outLeft[139] == 0.5f, "Five-millisecond ramp spans variable blocks and reaches exact target.");
    for (int i = 0; i < 32; ++i) require(engine.setGain(1, 2, 0.5f).succeeded, "Bounded parameter queue.");
    require(!engine.setGain(1, 2, 1).succeeded, "Queue saturation explicitly fails.");
    require(engine.publish(chain(0.25f), 2).succeeded, "Prepare newer graph off callback.");
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0.125f && engine.telemetry().value->activeRevision == 2
            && engine.telemetry().value->canceledGainChanges == 32, "Swap complete plan and cancel old-revision events.");
    require(!engine.publish(chain(), 2).succeeded, "Older/equal plans cannot displace newer revisions.");
    auto invalidGraph = chain();
    invalidGraph.connections.push_back({2, 2});
    require(!engine.publish(invalidGraph, 3).succeeded, "Preparation failure preserves active graph.");
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0.125f, "Last valid plan remains active on failed preparation.");
    require(engine.publish(chain(0.75f), 4).succeeded && engine.publish(chain(1), 5).succeeded,
            "Pending latest-wins publication reclaims superseded pending plans off callback.");
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0.5f && engine.telemetry().value->activeRevision == 5, "Newest pending graph becomes active.");
    require(engine.publish(chain(0.5f), 6).succeeded && engine.setGain(6, 2, 1).succeeded,
            "Accept live gain for a prepared but not yet active revision.");
    render(engine, buffers.span(0));
    require(engine.telemetry().value->activeRevision == 6, "Zero-frame boundary activates a pending plan without advancing samples.");
    render(engine, buffers.span(240));
    require(buffers.outLeft[0] > 0.25f && buffers.outLeft[0] < 0.26f && buffers.outLeft[239] == 0.5f,
            "Future-plan gain is not cancelled; zero-frame callbacks do not consume its ramp.");
    require(engine.publish(AudioGraph{}, 7).succeeded, "Empty graph is an explicit valid silent plan.");
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0 && buffers.outRight[0] == 0, "Empty graph clears outputs.");
    auto tooLarge = buffers.span(513);
    render(engine, tooLarge);
    require(buffers.outLeft[512] == 0 && !engine.telemetry().value->running
            && engine.telemetry().value->invalidBlocks == 1, "Oversized blocks are silenced without allocation and latch a stop fault.");
    require(engine.stop().succeeded && engine.stop().succeeded, "Repeated stop safely reclaims resources.");
    buffers.outLeft.fill(99);
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0, "A stopped renderer never leaves stale output.");
    auto silentInput = format(); silentInput.inputChannels = 0;
    require(engine.startOffline(silentInput, chain(), 1).succeeded, "Explicit output-only stream supplies silence to Input.");
    auto span = buffers.span(128); span.inputs = nullptr; span.inputChannels = 0;
    render(engine, span);
    require(buffers.outLeft[0] == 0, "No hidden default input is opened.");
    engine.interrupted(AudioStreamStop::deviceLost);
    render(engine, span);
    require(!engine.telemetry().value->running && engine.telemetry().value->stopReason == AudioStreamStop::deviceLost,
            "Device loss only signals atomics; processing stays silent.");
    require(engine.stop().succeeded, "End synthetic processing session.");
    auto mono = chain(); mono.channels = 1;
    require(engine.startOffline({8000, 1, 1, 1, 1}, mono, 1).succeeded, "Minimum one-frame mono configuration is valid.");
    span = buffers.span(1); span.inputChannels = 1; span.outputChannels = 1;
    render(engine, span);
    require(buffers.outLeft[0] == 0.25f, "Minimum configuration performs actual samples.");
    span.outputChannels = 2;
    render(engine, span);
    require(buffers.outLeft[0] == 0 && buffers.outRight[0] == 0
            && engine.telemetry().value->stopReason == AudioStreamStop::formatChanged,
            "A changed layout latches bounded silence rather than implicit conversion.");
    engine.interrupted(AudioStreamStop::deviceError);
    engine.interrupted(AudioStreamStop::requested);
    require(engine.telemetry().value->stopReason == AudioStreamStop::formatChanged,
            "Later cleanup/fault notifications preserve the first actionable fault until restart.");
}

void testValidation()
{
    const auto reject = [](AudioGraph graph) { return !PreparedAudioGraph::prepare(graph, format(), 1).value; };
    auto graph = chain();
    graph.nodes.push_back(graph.nodes.front()); require(reject(graph), "Reject duplicate identities.");
    graph = chain(); graph.connections.push_back({1, 3}); require(reject(graph), "Reject implicit fan-in.");
    graph = chain(); graph.connections[0].source = 99; require(reject(graph), "Reject corrupt references.");
    graph = chain(); graph.connections.push_back({3, 1}); require(reject(graph), "Reject invalid Input/Output port directions.");
    graph = chain(); graph.nodes.push_back({4, AudioNodeType::gain}); require(reject(graph), "Reject disconnected unused nodes.");
    graph = chain(); graph.channels = 1; require(reject(graph), "No implicit mono/stereo conversion.");
    graph = chain(); graph.nodes[1].gain = std::numeric_limits<float>::infinity(); require(reject(graph), "Reject nonfinite gain.");
    graph = chain(); graph.connections = {{2, 2}, {2, 3}}; require(reject(graph), "Reject self-feedback.");
    graph = chain(1, 2); graph.connections = {{2, 3}, {3, 2}, {3, 4}}; require(reject(graph), "Reject multi-node cycles.");
    graph = chain(1, 255); require(reject(graph), "Enforce node budget.");
    auto configuration = format(); configuration.maxFrames = 4097;
    require(!PreparedAudioGraph::prepare(chain(), configuration, 1).value, "Enforce capacity budget.");
    configuration = {48000, 128, 4096, 32, 32}; graph = chain(1, 254); graph.channels = 32;
    require(!PreparedAudioGraph::prepare(graph, configuration, 1).value, "Enforce scratch memory budget.");
    val::domain::Graph structural;
    structural.nodes.emplace(1, val::domain::Node{1, "Input"});
    require(!AudioGraph::fromStructural(structural, 2).value, "Names never implicitly confer DSP behavior.");
    structural.nodes.at(1).parameters["audio.type"] = std::string("Input");
    structural.nodes.emplace(2, val::domain::Node{2, "Gain"});
    structural.nodes.at(2).parameters["audio.type"] = std::string("Gain");
    structural.nodes.at(2).parameters["audio.gain"] = 0.25;
    structural.nodes.emplace(3, val::domain::Node{3, "Output"});
    structural.nodes.at(3).parameters["audio.type"] = std::string("Output");
    structural.connections.emplace(4, val::domain::Connection{4, {1, val::domain::Port::output}, {2, val::domain::Port::input}});
    structural.connections.emplace(5, val::domain::Connection{5, {2, val::domain::Port::output}, {3, val::domain::Port::input}});
    auto mapped = AudioGraph::fromStructural(structural, 2);
    require(mapped.value && mapped.value->nodes[1].gain == 0.25f
            && PreparedAudioGraph::prepare(*mapped.value, format(), 1).value.has_value(),
            "Explicit stable-ID structural mapping prepares a real runtime graph.");
    structural.nodes.at(2).parameters["audio.type"] = std::string("Plugin");
    require(!AudioGraph::fromStructural(structural, 2).value, "No unrequested plugin processing.");
}

thread_local std::uint64_t ticks{};
std::uint64_t measuredClock() noexcept { ticks += 2000000; return ticks; }
void testTelemetry()
{
    AudioEngine engine(measuredClock);
    Buffers buffers;
    require(engine.startOffline(format(), chain(), 1).succeeded, "Start deterministic telemetry.");
    render(engine, buffers.span(48));
    const auto report = *engine.telemetry().value;
    require(report.callbacks == 1 && report.frames == 48 && report.callbackNanoseconds == 2000000
            && report.maxCallbackNanoseconds == 2000000 && report.deadlineMisses == 1
            && report.lastCallbackLoad == 2.0 && report.averageCallbackLoad == 2.0,
            "Duration/load/deadline use actual frames and injected monotonic nanoseconds, not nominal periods.");
    require(!report.backendXruns && !report.reportedOutputLatencySeconds && !report.wholeProcessCpu,
            "Unknown backend xruns, physical/device latency and process CPU are never reported as zero.");
    bool rejected{};
    std::thread foreign([&] { rejected = !engine.publish(chain(), 2).succeeded && !engine.stop().succeeded && !engine.telemetry().value; });
    foreign.join();
    require(rejected, "Foreign thread cannot mutate/control engine.");
}

void testStress()
{
    AudioEngine engine;
    auto graph = chain(0.25f, 62); // 64 total processors.
    require(engine.startOffline(format(), graph, 1).succeeded, "Start declared 64-node synthetic workload.");
    std::atomic<bool> done{}, failed{};
    std::atomic<std::uint64_t> callbacks{};
    const auto began = std::chrono::steady_clock::now();
    std::thread callback([&]
    {
        Buffers buffers;
        do
        {
            std::uint64_t allocations{}, frees{};
            {
                allocationProbe::Scope scope;
                engine.process(buffers.span(128));
                allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
            }
            if (allocations || frees || (buffers.outLeft[0] != 0.125f && buffers.outLeft[0] != 0.375f))
                failed.store(true);
            callbacks.fetch_add(1);
        } while (!done.load(std::memory_order_acquire));
    });
    bool published = true;
    for (std::uint64_t revision = 2; revision <= 2001; ++revision)
    {
        graph.nodes[1].gain = revision % 2 ? 0.25f : 0.75f;
        if (!engine.publish(graph, revision).succeeded) published = false;
        if (!engine.collectRetired().succeeded) published = false;
    }
    done.store(true, std::memory_order_release);
    callback.join();
    require(published && !failed.load() && callbacks.load() > 0, "Concurrent graph swaps stay numerically valid and allocate/free nothing on render thread.");
    Buffers final;
    render(engine, final.span(128));
    const auto report = *engine.telemetry().value;
    require(report.activeRevision == 2001 && engine.collectRetired().succeeded, "Final revision activates and retired plans reclaim off callback.");
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::cout << "Synthetic 64-node/128-frame stereo stress: 2000 publications, " << callbacks.load()
              << " render callbacks in " << seconds << " s; last/max callback ns "
              << report.callbackNanoseconds << "/" << report.maxCallbackNanoseconds
              << "; average callback deadline load " << report.averageCallbackLoad
              << "; callback misses " << report.deadlineMisses << "; no backend/hardware active.\n";
}
}

int main()
{
    return runTests([] { testValidation(); testProcessing(); testTelemetry(); testStress(); });
}
