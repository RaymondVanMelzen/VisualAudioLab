#include "TestSupport.h"
#include <val/audio/AudioGraph.h>
#include <val/infrastructure/LocalProjectStore.h>
#include <filesystem>
#include <map>

int main()
{
    return runTests([]
    {
    using namespace val;
    const std::filesystem::path examples(VAL_EXAMPLE_PROJECT_DIR);
    infrastructure::LocalProjectStore store(examples / "recent-test.json");
    std::size_t projects{};
    for (const auto& entry : std::filesystem::directory_iterator(examples))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".valab") continue;
        const auto loaded = store.read(entry.path());
        require(loaded.file.has_value(), "Example project loads: " + entry.path().string());
        ++projects;
        for (const auto& [patchId, patch] : loaded.file->state.patches)
        {
            (void) patchId;
            std::map<std::uint64_t, std::vector<int>> channelMaps;
            int outputChannels{};
            for (const auto& [id, endpoint] : patch.graph.endpoints)
                if (endpoint.role == domain::EndpointRole::hardwareOutput && endpoint.enabled)
                {
                    auto& mapping = channelMaps[id];
                    for (int channel = 0; channel < endpoint.layoutChannels; ++channel)
                        mapping.push_back(outputChannels++);
                }
            require(outputChannels > 0, "Example project has at least one enabled output lane.");
            const auto mapped = audio::AudioGraph::fromStructural(patch.graph, outputChannels, channelMaps);
            require(mapped.value.has_value(), "Every saved example graph maps to an executable audio graph: "
                    + entry.path().string() + " (" + mapped.error.message + ").");
            const auto prepared = audio::PreparedAudioGraph::prepare(*mapped.value,
                {48000, 512, 512, 0, outputChannels}, 1);
            require(prepared.value.has_value(), "Every example can prepare offline without device access or playback: "
                    + entry.path().string() + " (" + prepared.error.message + ").");
        }
    }
    require(projects == 8, "Exactly eight runnable toolkit example projects are checked.");
    std::cout << "All eight toolkit project files load, map and prepare offline. No device was opened.\n";
    });
}
