#include "TestSupport.h"
#include <val/presentation/MainComponent.h>
#include <val/audio/AudioEngine.h>
#include <chrono>
#include <numbers>
#include <thread>

namespace
{
/** Shows the real host/components without acquiring audio hardware. */
class VisualizationTestWindow final : public juce::DocumentWindow
{
public:
    explicit VisualizationTestWindow(juce::Component& content)
        : DocumentWindow("Visual Audio Lab - observation validation", juce::Colour(0xff141a23), closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
        centreWithSize(content.getWidth(), content.getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
}

void runVisualizationPresentationTests()
{
    using namespace val::presentation;
    using namespace val::audio;
    val::application::WorkspaceService workspace;
    MainComponent shell(workspace);
    shell.setSize(1280, 800);
    VisualizationTestWindow window(shell);
    auto& session = shell.dockWorkspace().visualizations();
    auto& inspector = shell.dockWorkspace().signalPreview();
    auto capture = std::make_shared<SignalCaptureBuffer>(7, 48000, 2);
    require(session.add(capture, "Gain output - offline calibration fixture").succeeded, "Register real graph observation source.");
    inspector.setTarget(7);
    inspector.setPinned(true);
    inspector.followSelection(99);
    require(inspector.target() == 7, "Pin separates inspector target from canvas selection.");
    inspector.setPinned(false);
    inspector.followSelection(99);
    require(inspector.target() == 99, "Unpinned inspector follows selection without changing audio graph.");
    inspector.setTarget(7);
    require(!inspector.plot(VisualizationKind::spectrum).setRefreshHz(31).succeeded
            && inspector.plot(VisualizationKind::spectrum).setRefreshHz(15).succeeded,
            "Each view validates its refresh ceiling.");
    shell.dockWorkspace().showPanel(PanelId::propertyInspector);
    juce::TabbedComponent* inspectorTabs{};
    for (auto* child : shell.dockWorkspace().panel(PanelId::propertyInspector).getChildren())
        if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(child)) inspectorTabs = tabs;
    require(inspectorTabs, "Property Inspector hosts distinct parameter/script and Signal tabs.");
    inspectorTabs->setCurrentTabIndex(1);
    session.refresh();
    require(inspector.demanding(), "Visible selected inspector requests one shared observation.");
    inspectorTabs->setCurrentTabIndex(0); session.refresh();
    require(!inspector.demanding() && !capture->enabled(), "Hidden Signal tab releases observation demand.");
    inspectorTabs->setCurrentTabIndex(1); session.refresh();
    require(inspector.demanding(), "Returning to Signal restores observation demand.");
    inspector.setFrozen(true); session.refresh();
    require(!inspector.demanding() && !capture->enabled(), "Last frozen view detaches capture without retaining a result-page lease.");
    inspector.setFrozen(false);
    session.refresh();
    AudioGraph graph{2, {{1, AudioNodeType::input}, {7, AudioNodeType::gain, 0.5f}, {9, AudioNodeType::output}}, {{1, 7}, {7, 9}}};
    graph.nodes[1].capture = capture;
    AudioEngine engine;
    require(engine.startOffline({48000, 512, 512, 2, 2}, graph, 1).succeeded, "Prepare actual Gain processing without opening hardware.");
    std::array<float, 1024> input{}, output{};
    const float* inputs[]{input.data(), input.data() + 512};
    float* outputs[]{output.data(), output.data() + 512};
    for (int block = 0; block < 60; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            input[i] = static_cast<float>(std::sin(2 * std::numbers::pi * 937.5 * (block * 512 + i) / 48000));
            input[i + 512] = input[i] * 0.5f;
        }
        engine.process({inputs, outputs, 2, 2, 512});
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        session.refresh();
    }
    VisualizationWidgets detail(session, VisualizationHost::monitor);
    detail.setSize(960, 600);
    VisualizationTestWindow detailsWindow(detail);
    detail.setTarget(7); detail.setPinned(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60)); session.refresh();
    auto snapshot = detail.createComponentSnapshot(detail.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
    require(snapshot.isValid(), "Actual calibrated JUCE scope/spectrum/peak/RMS widgets render.");
    const auto path = juce::SystemStats::getEnvironmentVariable("VAL_VISUALIZATION_SCREENSHOT", {});
    if (path.isNotEmpty())
    {
        const auto outputFile = juce::File(path);
        auto stream = outputFile.createOutputStream();
        require(stream && stream->setPosition(0) && stream->truncate().wasOk()
                && juce::PNGImageFormat{}.writeImageToStream(snapshot, *stream), "Persist actual widget screenshot, replacing an earlier run.");
        stream->flush();
        require(stream->getStatus().wasOk(), "Screenshot writes reach storage without I/O errors.");
    }
    inspector.setVisible(false); session.refresh();
    require(!inspector.demanding() && capture->enabled(), "Hidden inspector stops demand while pinned visible detail continues sharing.");
    detail.setFrozen(true); session.refresh();
    require(!capture->enabled(), "Freezing the last monitor stops graphical sample traffic.");
    detail.setFrozen(false); inspector.setVisible(true); session.refresh();
    session.setState(7, ObservationState::stopped); session.refresh();
    require(!capture->enabled(), "Explicit audio stopped state disables UI demand, not measured zero.");
    session.setState(7, ObservationState::processing);
    session.refresh();
    engine.process({inputs, outputs, 2, 2, 512});
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); session.refresh();
    require(engine.stop().succeeded, "Quiesce engine before removing observation ownership.");
    inspector.setVisible(false); detail.setVisible(false); session.refresh();
    for (int i = 0; i < 7; ++i)
        require(workspace.createNode({50.0 + (i % 3) * 190.0, 40.0 + (i / 3) * 100.0}).succeeded, "Create structural preview fixture only in test.");
    auto& canvas = shell.dockWorkspace().canvas();
    canvas.synchronise();
    canvas.fitGraph();
    canvas.zoomAt(1, canvas.viewport().getCentre().toFloat());
    session.refresh();
    require(capture->enabled() && canvas.observationVisible(7), "Visible scheduled canvas node shares the existing captured product.");
    require(canvas.createComponentSnapshot(canvas.getLocalBounds()).isValid(), "Actual embedded waveform renders through culled canvas paint.");
    canvas.zoomAt(0.5f, canvas.viewport().getCentre().toFloat()); session.refresh();
    require(!capture->enabled(), "Semantic zoom removes preview demand instead of running hidden analysis.");
    session.bindCanvas(nullptr);

    // Reproducible visible 100-preview + 8-monitor render stress, using one source/product.
    juce::Component wall;
    wall.setSize(3200, 2000);
    std::vector<std::unique_ptr<VisualizationWidgets>> hosts;
    for (int i = 0; i < 108; ++i)
    {
        auto host = std::make_unique<VisualizationWidgets>(session, i < 100 ? VisualizationHost::embedded : VisualizationHost::monitor);
        host->setTarget(7);
        if (i < 100) host->setBounds(i % 10 * 160, i / 10 * 120, 160, 120);
        else host->setBounds(1600 + (i - 100) % 2 * 800, (i - 100) / 2 * 500, 800, 500);
        wall.addAndMakeVisible(*host);
        hosts.push_back(std::move(host));
    }
    // Hide the two review hosts so demand admission is exactly 100+8.
    inspector.setVisible(false); detail.setVisible(false); session.refresh();
    VisualizationTestWindow wallWindow(wall);
    session.refresh();
    for (const auto& host : hosts) require(host->demanding(), "Each visible stress host shares one admitted source.");
    require(!hosts.front()->plot(VisualizationKind::oscilloscope).setRefreshHz(16).succeeded,
            "Embedded previews are capped at 15 Hz.");
    std::vector<double> renderTimes;
    for (int frame = 0; frame < 12; ++frame)
    {
        const auto began = std::chrono::steady_clock::now();
        auto rendered = wall.createComponentSnapshot(wall.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
        require(rendered.isValid(), "All actual stress components render without per-node FFT/timers.");
        renderTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
    }
    std::sort(renderTimes.begin(), renderTimes.end());
    const auto diagnostics = session.diagnostics();
    std::cout << "SYNTHETIC_SOFTWARE_RENDER visible_embedded=100 detailed=8 shared_sources=" << diagnostics.sources
              << " wall_pixels=3200x2000 frames=12 frame_p50_ms=" << renderTimes[6]
              << " frame_max_ms=" << renderTimes.back()
              << " observation_bytes=" << diagnostics.memoryBytes << '\n';
    double plotMax = 0;
    for (const auto& host : hosts)
        for (int kind = 0; kind < 4; ++kind)
            plotMax = std::max(plotMax, host->plot(static_cast<VisualizationKind>(kind)).maxPaintMilliseconds());
    std::cout << "individual_plot_max_ms=" << plotMax << " worker_jobs=" << diagnostics.jobs
              << " worker_max_ms=" << diagnostics.maxJobMilliseconds << " capture_drops=" << capture->drops() << '\n';
    wallWindow.setVisible(false); session.refresh();
    require(!capture->enabled(), "Entire offscreen/hidden host tree releases all shared demand.");
    hosts.clear();
    session.remove(7); session.refresh();
    require(!capture->enabled(), "Removed target cannot retain graphical capture demand.");
}
