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
    graph = chain(); graph.nodes.push_back({4, AudioNodeType::gain}); require(!reject(graph), "Disconnected unused nodes are legal and inert.");
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

void testExplicitRouting()
{
    AudioEngine engine;
    Buffers buffers;
    AudioGraph graph;
    graph.nodes = {{1, AudioNodeType::input}, {2, AudioNodeType::gain, 0.5f},
                   {3, AudioNodeType::gain, 0.25f}, {4, AudioNodeType::sum},
                   {5, AudioNodeType::output}, {6, AudioNodeType::dcGenerator}};
    graph.connections = {{1, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 5}};
    const auto inactiveCapture = std::make_shared<SignalCaptureBuffer>(6, 48000, 2);
    inactiveCapture->setEnabled(true);
    graph.nodes[5].capture = inactiveCapture;
    const auto sumCapture = std::make_shared<SignalCaptureBuffer>(4, 48000, 2);
    sumCapture->setEnabled(true);
    graph.nodes[3].capture = sumCapture;
    require(engine.startOffline(format(), graph, 1).succeeded, "Fan-out branches reunite only at explicit Sum.");
    render(engine, buffers.span(128));
    require(buffers.outLeft[0] == 0.375f && buffers.outRight[127] == -0.1875f,
            "Sum adds corresponding channels with unity coefficients, without implicit averaging.");
    require(inactiveCapture->capturedFrames() == 0 && sumCapture->capturedFrames() == 128,
            "Inactive branches do not publish observations; Sum participates in ordinary bounded capture.");
    require(sumCapture->acquire() != nullptr, "Sum capture is readable.");
    sumCapture->release();
    auto active = graph.outputDependencies();
    require(active == std::vector<std::uint64_t>({1, 2, 3, 4, 5}), "Unrouted generator is excluded from output dependencies.");
    graph.connections.push_back({2, 4});
    require(!engine.publish(graph, 2).succeeded, "Duplicate cables cannot double a signal, even at Sum.");
    graph.connections.pop_back();
    graph.nodes[3].bypassed = true;
    require(!engine.publish(graph, 2).succeeded, "Sum cannot be bypassed to select an arbitrary upstream source.");
    graph.nodes[3].bypassed = false;
    graph.connections = {{1, 5}};
    require(engine.publish(graph, 2).succeeded, "Direct source-to-destination plus inert processor islands is valid.");
    render(engine, buffers.span(17));
    require(buffers.outLeft[0] == 0.5f && buffers.outRight[16] == -0.25f, "Direct route preserves channels.");
    auto inactiveCycle = graph;
    inactiveCycle.nodes.push_back({7, AudioNodeType::gain});
    inactiveCycle.connections.push_back({2, 7});
    inactiveCycle.connections.push_back({7, 2});
    require(!engine.publish(inactiveCycle, 3).succeeded, "Cycles are rejected even in inactive islands.");
    require(engine.stop().succeeded, "Stop routing plan.");
    graph.nodes = {{1, AudioNodeType::dcGenerator}, {2, AudioNodeType::dcGenerator},
                   {3, AudioNodeType::sum}, {4, AudioNodeType::output}};
    graph.nodes[0].parameters = {{std::uint16_t{0}, 0.25f}};
    graph.nodes[1].parameters = {{std::uint16_t{0}, -0.125f}};
    graph.connections = {{1, 3}, {2, 3}, {3, 4}};
    auto noInput = format(); noInput.inputChannels = 0;
    require(engine.startOffline(noInput, graph, 1).succeeded, "Multiple independent zero-input roots feed the main destination.");
    auto span = buffers.span(128); span.inputs = nullptr; span.inputChannels = 0;
    render(engine, span);
    require(buffers.outLeft[127] == 0.125f && buffers.outRight[0] == 0.125f, "Independent roots are each evaluated before Sum.");
    require(engine.setParameter(1, 1, 0, 0.5f, ParameterAction::base).succeeded,
            "Multiple-root route accepts sample-time parameter changes.");
    render(engine, span);
    graph.connections = {{1, 3}, {2, 3}, {3, 4}, {3, 2}};
    require(!engine.publish(graph, 2).succeeded, "Sources reject incoming edges.");
    graph.nodes.push_back({5, AudioNodeType::output});
    require(!engine.publish(graph, 2).succeeded, "Legacy identity-mapped destinations cannot overlap host channels.");
    require(engine.stop().succeeded, "Stop initial explicit-sum route.");

    AudioGraph multiOutput;
    AudioNodeDefinition source{10, AudioNodeType::dcGenerator};
    source.channels = 1;
    source.parameters = {{0, 0.25f}};
    AudioNodeDefinition left{11, AudioNodeType::output};
    left.channels = 1; left.hardwareEndpoint = true; left.hardwareChannelMap = {1};
    AudioNodeDefinition right{12, AudioNodeType::output};
    right.channels = 1; right.hardwareEndpoint = true; right.hardwareChannelMap = {3};
    multiOutput.nodes = {source, left, right};
    multiOutput.connections = {{10, 11}, {10, 12}};
    std::array<float, 32> channel0{}, channel1{}, channel2{}, channel3{};
    float* outputs[]{channel0.data(), channel1.data(), channel2.data(), channel3.data()};
    AudioBuffer fourChannelOutput{nullptr, outputs, 0, 4, 8};
    const auto multiOutputStarted = engine.startOffline({48000, 128, 512, 0, 4}, multiOutput, 1);
    require(multiOutputStarted.succeeded, multiOutputStarted.error);
    render(engine, fourChannelOutput);
    require(channel0[0] == 0 && channel1[7] == 0.25f && channel2[0] == 0 && channel3[7] == 0.25f,
            "Sparse output rows write only their exact compact stream channels.");
    auto overlap = multiOutput;
    overlap.nodes[2].hardwareChannelMap = {1};
    require(!engine.publish(overlap, 2).succeeded, "Overlapping output endpoint rows fail explicitly.");
    require(engine.stop().succeeded, "Stop multi-output routing.");

    AudioGraph adapted;
    AudioNodeDefinition mono{20, AudioNodeType::dcGenerator};
    mono.channels = 1; mono.parameters = {{0, -0.5f}};
    AudioNodeDefinition stereo{21, AudioNodeType::output};
    stereo.channels = 2; stereo.hardwareEndpoint = true; stereo.hardwareChannelMap = {0, 2};
    adapted.nodes = {mono, stereo};
    adapted.connections = {{20, 21, val::domain::AudioAdapter::monoToStereo}};
    require(engine.startOffline({48000, 128, 512, 0, 3}, adapted, 1).succeeded,
            "Explicit dual-mono adapter connects a mono source to a stereo destination.");
    std::array<float, 32> adapted0{}, adapted1{}, adapted2{};
    float* adaptedOutputs[]{adapted0.data(), adapted1.data(), adapted2.data()};
    AudioBuffer adaptedBuffer{nullptr, adaptedOutputs, 0, 3, 8};
    render(engine, adaptedBuffer);
    require(adapted0[0] == -0.5f && adapted1[0] == 0 && adapted2[0] == -0.5f,
            "Explicit adapter duplicates mono into the destination's selected channels only.");
    auto implicit = adapted; implicit.connections[0].adapter = val::domain::AudioAdapter::none;
    require(!engine.publish(implicit, 2).succeeded, "Unequal source and destination widths require a visible adapter.");
    require(engine.stop().succeeded, "Stop adapter routing.");

    AudioGraph sparseInput;
    AudioNodeDefinition input{30, AudioNodeType::input};
    input.channels = 1; input.hardwareEndpoint = true; input.hardwareChannelMap = {1};
    AudioNodeDefinition output{31, AudioNodeType::output};
    output.channels = 1; output.hardwareEndpoint = true; output.hardwareChannelMap = {0};
    sparseInput.nodes = {input, output};
    sparseInput.connections = {{30, 31}};
    std::array<float, 16> input0{}, input1{}, output0{};
    input0.fill(0.125f); input1.fill(-0.75f);
    const float* sparseInputs[]{input0.data(), input1.data()};
    float* sparseOutputs[]{output0.data()};
    AudioBuffer sparseBuffer{sparseInputs, sparseOutputs, 2, 1, 8};
    require(engine.startOffline({48000, 128, 512, 2, 1}, sparseInput, 1).succeeded,
            "Input and output endpoint widths are independent and may be sparse.");
    render(engine, sparseBuffer);
    require(output0[0] == -0.75f, "Input map selects the exact named channel packed by the stream mapping.");
    require(engine.stop().succeeded, "Stop sparse endpoint route.");
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
    return runTests([] { testValidation(); testExplicitRouting(); testProcessing(); testTelemetry(); testStress(); });
}
