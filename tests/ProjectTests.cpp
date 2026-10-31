#include "TestSupport.h"
#include <val/application/ProjectSession.h>
#include <val/infrastructure/LocalProjectStore.h>
#include <juce_core/juce_core.h>
#include <limits>

namespace
{
/** Per-run isolated files; only this explicitly created temporary directory is removed. */
class ProjectDirectory final
{
public:
    ProjectDirectory() : file(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("val-project-" + juce::Uuid().toString()))
    {
        require(file.createDirectory().wasOk(), "Create project test directory.");
    }
    ~ProjectDirectory() { if (!file.deleteRecursively()) std::cerr << "Project test cleanup failed.\n"; }
    std::filesystem::path path() const
    {
        const auto utf8 = file.getFullPathName().toStdString();
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }
    juce::File file;
};

/** Storage failure injection checks lifecycle invariants without relying on filesystem timing. */
class FailingStore final : public val::application::ProjectStore
{
public:
    val::application::ProjectReadResult read(const std::filesystem::path&) override { return {{}, "Injected read failure."}; }
    val::application::Result write(const std::filesystem::path&, val::application::ProjectFile&,
                                  const std::optional<std::string>&) override
    { return val::application::Result::failure("Injected write/flush/commit failure."); }
    val::application::Result readRecent(std::vector<std::filesystem::path>&) override
    { return val::application::Result::success(); }
    val::application::Result writeRecent(const std::vector<std::filesystem::path>&) override
    { return val::application::Result::failure("Injected recent metadata failure."); }
};

juce::var parsed(const std::string& source)
{
    return juce::JSON::parse(juce::String::fromUTF8(source.c_str()));
}
std::string json(const juce::var& value) { return juce::JSON::toString(value, false, 17).toStdString(); }
void put(juce::var& value, const char* key, juce::var field) { value.getDynamicObject()->setProperty(key, std::move(field)); }

void testProjects(const ProjectDirectory& directory)
{
    using namespace val;
    using namespace application;
    using infrastructure::LocalProjectStore;
    LocalProjectStore store(directory.path() / "recent.json");
    WorkspaceService workspace;
    ProjectSession session(workspace, store);
    require(!session.dirty() && !session.save().succeeded, "Untitled clean document needs Save As.");
    require(workspace.rename("Portable project").succeeded, "Rename project.");
    require(workspace.createNode({-120, 10}, "Source").succeeded, "Create first node.");
    const auto a = workspace.lastCreatedNode();
    require(workspace.createNode({300, 10}, "Destination").succeeded, "Create second node.");
    const auto b = workspace.lastCreatedNode();
    require(workspace.connect({a, domain::Port::output}, {b, domain::Port::input}).succeeded, "Connect nodes.");
    require(workspace.setParameter(a, "gain", 0.125).succeeded
            && workspace.setParameter(a, "enabled", true).succeeded
            && workspace.setParameter(a, "label", std::string("hello")).succeeded
            && workspace.setParameter(a, "unset", std::monostate{}).succeeded, "All parameter scalar types.");
    require(workspace.setParameter(a, "precise", 0.12345678901234566).succeeded, "Binary64 parameter precision.");
    const auto parameters = workspace.graph().nodes.at(a).parameters;
    require(workspace.undo().succeeded && !workspace.graph().nodes.at(a).parameters.contains("precise")
            && workspace.redo().succeeded && workspace.graph().nodes.at(a).parameters == parameters,
            "Parameter edits share undo/redo.");
    require(workspace.addPatch("Second patch").succeeded, "Add second patch.");
    const auto second = workspace.project().activePatch;
    require(workspace.createNode({12, 34}, "Other").succeeded, "Populate second patch.");
    const auto nextNode = workspace.lastCreatedNode();
    require(workspace.selectPatch(1).succeeded && workspace.undo().succeeded
            && workspace.project().activePatch == second && workspace.graph().nodes.empty()
            && workspace.redo().succeeded, "Undo/redo follows its owning patch across navigation.");
    workspace.setViewport({0.5, 600, -200});
    auto layout = workspace.project().layout;
    layout.x = -800; layout.y = 170; layout.width = 1450; layout.height = 950;
    layout.docks[2] = 2; layout.visible[4] = false; layout.selectedPanels[3] = -1;
    layout.ratios = {0.25, 0.30, 0.40};
    workspace.setLayout(layout);
    const auto original = workspace.project();
    const auto unicodePath = directory.path() / std::filesystem::path(u8"project-\u00e9-\u97f3.valab");
    require(session.dirty() && session.saveAs(unicodePath).succeeded && !session.dirty(), "Save As Unicode project.");
    const auto loaded = store.read(unicodePath);
    require(loaded.file && loaded.file->state == original, "All patches, graphs, parameters and layout round-trip.");
    require(loaded.file->source.find('\n') != std::string::npos, "JSON is human readable.");
    require(session.recent().size() == 1 && session.save().succeeded && session.recent().size() == 1,
            "Save deduplicates Recent Files.");
    require(workspace.rename("Changed").succeeded && session.dirty() && workspace.undo().succeeded && !session.dirty(),
            "Dirty state follows saved content through undo.");
    workspace.setViewport({1.25, 10, 20});
    require(!session.dirty(), "Navigation is not a content dirty edit.");
    require(session.load(unicodePath).succeeded && workspace.project() == original
            && !workspace.canUndo() && !workspace.canRedo(), "Load replaces atomically and resets old history.");
    require(workspace.createNode({0, 0}).succeeded && workspace.lastCreatedNode() > nextNode,
            "Loaded identities are not reused.");

    const auto activeBefore = workspace.project();
    const auto pathBefore = session.path();
    require(!session.load(directory.path() / "missing.valab").succeeded
            && workspace.project() == activeBefore && session.path() == pathBefore && workspace.canUndo(),
            "Missing load preserves active content/path/history.");
    require(!session.saveAs(directory.path() / "absent" / "project.valab").succeeded
            && session.path() == pathBefore && session.dirty(), "Failed Save As preserves path and dirty content.");
    const auto externalFile = directory.file.getChildFile(juce::String::fromUTF8(
        reinterpret_cast<const char*>(unicodePath.filename().u8string().c_str())));
    require(externalFile.appendText("\n"), "Simulate external modification.");
    const auto externalContent = externalFile.loadFileAsString();
    require(!session.save().succeeded && externalFile.loadFileAsString() == externalContent
            && session.dirty(), "External-change detection protects the existing active file.");
    require(session.saveAs(directory.path() / "copy.valab").succeeded && !session.dirty(),
            "Save As recovers to a different location.");

    ProjectFile opaque = *loaded.file;
    auto root = parsed(opaque.source);
    auto version = root["version"]; put(version, "minor", 7);
    put(root, "futureRoot", parsed(R"({"nested":[1,true,"keep"]})"));
    auto patch = root["patches"].getArray()->getReference(0);
    put(patch, "futurePatch", "retained");
    auto graph = patch["graph"]; put(graph, "futureGraph", 42);
    auto node = graph["nodes"].getArray()->getReference(0); put(node, "futureNode", "retained");
    auto position = node["position"]; put(position, "futurePosition", "retained");
    auto connection = graph["connections"].getArray()->getReference(0); put(connection, "futureWire", "retained");
    auto endpoint = connection["source"]; put(endpoint, "futureEndpoint", true);
    auto window = root["layout"]["window"]; put(window, "futureWindow", 123);
    auto panels = root["layout"]["panels"];
    auto panel = panels.getArray()->getReference(0); put(panel, "futurePanel", "retained");
    panels.getArray()->add(parsed(R"({"id":"futureTool","arbitrary":{"data":true}})"));
    opaque.source = json(root);
    auto future = LocalProjectStore::decode(opaque.source);
    require(future.file.has_value(), "Compatible additive future minor version opens.");
    future.file->state.workspace.name = "Edited future";
    future.file->state.patches.at(1).graph.nodes.at(a).position.x = 999;
    require(LocalProjectStore::encode(*future.file).succeeded, "Read-edit-save future project.");
    const auto round = parsed(future.file->source);
    require(round["version"]["minor"] == juce::var(7) && json(round["futureRoot"]) == json(root["futureRoot"])
            && round["patches"][0]["futurePatch"].toString() == "retained"
            && round["patches"][0]["graph"]["futureGraph"] == juce::var(42)
            && round["patches"][0]["graph"]["nodes"][0]["futureNode"].toString() == "retained"
            && round["patches"][0]["graph"]["nodes"][0]["position"]["futurePosition"].toString() == "retained"
            && round["patches"][0]["graph"]["connections"][0]["futureWire"].toString() == "retained"
            && static_cast<bool>(round["patches"][0]["graph"]["connections"][0]["source"]["futureEndpoint"])
            && round["layout"]["window"]["futureWindow"] == juce::var(123)
            && round["layout"]["panels"][0]["futurePanel"].toString() == "retained"
            && round["layout"]["panels"].size() == 6, "Unknown data at every entity level is preserved.");
    const auto decodedFuture = LocalProjectStore::decode(future.file->source);
    require(decodedFuture.file.has_value(), decodedFuture.error);
    require(decodedFuture.file->state.patches.at(1).graph.nodes.at(a).position.x == 999,
            "Known edits are applied without discarding unknown fields.");
    const auto futurePath = directory.path() / "future.valab";
    require(store.write(futurePath, *future.file, std::nullopt).succeeded, "Write compatible future project fixture.");
    WorkspaceService futureWorkspace;
    ProjectSession futureSession(futureWorkspace, store);
    require(futureSession.load(futurePath).succeeded && futureWorkspace.selectPatch(1).succeeded
            && futureWorkspace.deleteNodes({a}).succeeded && futureSession.save().succeeded,
            "Save an intentional deletion of an entity carrying opaque metadata.");
    require(futureWorkspace.undo().succeeded && futureSession.save().succeeded
            && parsed(store.read(futurePath).file->source)["patches"][0]["graph"]["nodes"][0]["futureNode"].toString() == "retained",
            "Undo after a deletion has been saved restores unknown entity metadata as well as graph content.");

    const auto reject = [&](auto mutate)
    {
        auto value = parsed(loaded.file->source);
        mutate(value);
        const auto content = json(value);
        auto invalidFile = directory.file.getChildFile("invalid.valab");
        require(invalidFile.replaceWithText(juce::String::fromUTF8(content.c_str())), "Seed invalid project.");
        const auto before = workspace.project();
        const auto activePath = session.path();
        const auto couldUndo = workspace.canUndo();
        require(!session.load(directory.path() / "invalid.valab").succeeded
                && workspace.project() == before && session.path() == activePath && workspace.canUndo() == couldUndo,
                "Invalid loads preserve project/path/history.");
        require(!session.saveAs(directory.path() / "invalid.valab").succeeded
                && invalidFile.loadFileAsString().toStdString() == content,
                "Unsupported/corrupt target cannot be overwritten.");
    };
    reject([](auto& value) { auto v = value["version"]; put(v, "major", 2); });
    reject([](auto& value) { auto v = value["minimumReader"]; put(v, "minor", 1); });
    reject([](auto& value) { put(value, "requiredFeatures", juce::Array<juce::var>{"futureProcessor"}); });
    reject([](auto& value) { put(value, "activePatch", "999"); });
    reject([](auto& value) { auto n = value["patches"][0]["graph"]["nodes"][0]; put(n, "id", "01"); });
    reject([](auto& value) { auto e = value["patches"][0]["graph"]["connections"][0]["destination"]; put(e, "node", "999"); });
    reject([](auto& value) { auto e = value["patches"][0]["graph"]["connections"][0]["source"]; put(e, "port", "future"); });
    reject([](auto& value) { auto n = value["patches"][0]["graph"]["nodes"]; auto duplicate = n[0]; n.getArray()->add(duplicate); });
    reject([](auto& value)
    {
        auto wires = value["patches"][0]["graph"]["connections"];
        auto wire = parsed(R"({"id":"100","source":{"node":"2","port":"out"},"destination":{"node":"1","port":"in"}})");
        wires.getArray()->add(wire);
    });
    reject([](auto& value) { auto p = value["patches"][0]["graph"]["nodes"][0]["position"]; put(p, "x", "bad"); });
    reject([](auto& value) { auto p = value["patches"][0]["graph"]["nodes"][0]["parameters"]; put(p, "future", juce::Array<juce::var>{1}); });
    require(!LocalProjectStore::decode("{").file && !LocalProjectStore::decode("[]").file,
            "Malformed JSON and non-object roots fail.");
    require(!LocalProjectStore::decode(R"({"duplicate":1,"duplicate":2})").file
            && !LocalProjectStore::decode(R"({"number":9223372036854775808})").file
            && !LocalProjectStore::decode(R"({"number":0.1234567890123456789})").file
            && !LocalProjectStore::decode(R"({"number":1e-500})").file
            && !LocalProjectStore::decode(R"({"text":"\u0000"})").file,
            "Ambiguous or unsupported precision/NUL input fails rather than silently losing data.");
    ProjectFile invalid{original, {}};
    invalid.state.patches.at(1).graph.nodes.at(a).position.x = std::numeric_limits<double>::infinity();
    require(!LocalProjectStore::encode(invalid).succeeded, "Invalid in-memory snapshots cannot be serialized.");

    WorkspaceService failedWorkspace;
    FailingStore failing;
    ProjectSession failed(failedWorkspace, failing);
    require(failedWorkspace.createNode({1, 2}).succeeded, "Prepare failure fixture.");
    const auto beforeFailure = failedWorkspace.project();
    require(!failed.saveAs(directory.path() / "injected.valab").succeeded && failed.dirty()
            && failed.path().empty() && failedWorkspace.project() == beforeFailure
            && !failed.load(unicodePath).succeeded && failedWorkspace.canUndo(),
            "Injected read/write failure never accepts a snapshot/path or destroys history.");
    require(directory.file.getChildFile("directory.valab").createDirectory().wasOk()
            && !session.saveAs(directory.path() / "directory.valab").succeeded, "Directories cannot become project files.");
   #if JUCE_WINDOWS
    {
        const auto lockedPath = directory.path() / "copy.valab";
        juce::FileOutputStream held(directory.file.getChildFile("copy.valab"));
        require(held.openedOk(), "Hold a file without delete-sharing to inject Windows replacement failure.");
        auto candidate = *store.read(lockedPath).file;
        const auto originalBytes = candidate.source;
        candidate.state.workspace.name = "Must not replace locked destination";
        require(!store.write(lockedPath, candidate, originalBytes).succeeded
                && store.read(lockedPath).file->source == originalBytes && candidate.source == originalBytes,
                "A real native replacement failure preserves destination bytes and the adapter snapshot.");
    }
   #endif

    for (int i = 0; i < 12; ++i)
        require(session.saveAs(directory.path() / ("recent-" + std::to_string(i) + ".valab")).succeeded, "Recent fixture save.");
    require(session.recent().size() == 10, "Recent Files is bounded to ten.");
    ProjectSession restarted(workspace, store);
    require(restarted.recent() == session.recent(), "Recent Files survive restart.");
    require(restarted.forgetRecent(restarted.recent().front()).succeeded && restarted.recent().size() == 9,
            "Missing entries can be explicitly removed.");
    const auto recentBytes = directory.file.getChildFile("recent.json").loadFileAsString();
    require(!store.writeRecent(std::vector<std::filesystem::path>(11, "excess.valab")).succeeded
            && !store.writeRecent({std::filesystem::path(std::string(65536, 'x'))}).succeeded
            && directory.file.getChildFile("recent.json").loadFileAsString() == recentBytes,
            "Oversized Recent Files lists are rejected before replacing readable metadata.");
    require(directory.file.getChildFile("recent.json").replaceWithText("{"), "Seed broken recent metadata.");
    require(session.save().succeeded && !session.warning().empty()
            && directory.file.getChildFile("recent.json").loadFileAsString() == "{",
            "Recent metadata failure is explicit but does not misreport a successful project save.");
}
}

int main()
{
    return runTests([]
    {
        const ProjectDirectory directory;
        testProjects(directory);
    });
}
