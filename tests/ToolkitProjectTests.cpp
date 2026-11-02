#include "TestSupport.h"
#include <val/audio/AudioGraph.h>
#include <val/infrastructure/LocalProjectStore.h>
#include <filesystem>

int main()
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
            const auto mapped = audio::AudioGraph::fromStructural(patch.graph, 1);
            require(mapped.value.has_value(), "Every saved example graph maps to an executable audio graph: "
                    + entry.path().string() + " (" + mapped.error.message + ").");
            bool needsInput{};
            for (const auto& [nodeId, node] : patch.graph.nodes)
            {
                (void) nodeId;
                const auto type = node.parameters.find("audio.type");
                if (type != node.parameters.end())
                    if (const auto* name = std::get_if<std::string>(&type->second); name && *name == "Input")
                        needsInput = true;
            }
            const auto prepared = audio::PreparedAudioGraph::prepare(*mapped.value,
                {48000, 512, 512, needsInput ? 1 : 0, 1}, 1);
            require(prepared.value.has_value(), "Every example can prepare offline without device access or playback: "
                    + entry.path().string() + " (" + prepared.error.message + ").");
        }
    }
    require(projects == 8, "Exactly eight runnable toolkit example projects are checked.");
    std::cout << "All eight toolkit project files load, map and prepare offline. No device was opened.\n";
}
