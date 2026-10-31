#include "TestSupport.h"
#include <val/infrastructure/FileLogger.h>
#include <val/infrastructure/LocalSettingsStore.h>
#include <juce_core/juce_core.h>
#include <fstream>
#include <thread>

namespace
{
/** Unique per-run test directory; cleanup is restricted to that explicitly created child. */
class TestDirectory final
{
public:
    TestDirectory()
        : file(juce::File::getCurrentWorkingDirectory()
                   .getChildFile("val-tests-" + juce::Uuid().toString()))
    {
        require(file.createDirectory().wasOk(), "Create test directory.");
    }
    ~TestDirectory() { if (!file.deleteRecursively()) std::cerr << "Test directory cleanup failed.\n"; }
    std::filesystem::path path() const
    {
        const auto utf8 = file.getFullPathName().toStdString();
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }
    juce::File file;
};

void testSettings(const TestDirectory& directory)
{
    using namespace val::application;
    val::infrastructure::LocalSettingsStore store(directory.path() / "settings.json");
    val::infrastructure::LocalSettingsStore empty({});
    require(empty.load().state == SettingsLoadState::failed && !empty.save({}).succeeded,
            "An empty settings destination fails explicitly.");
    require(store.load().state == SettingsLoadState::missing, "Missing settings must be explicit.");
    const Settings value{1300, 800, false};
    require(store.save(value).succeeded, "Write valid preferences.");
    auto loaded = store.load();
    require(loaded.state == SettingsLoadState::loaded && loaded.value == value, "Preferences round-trip.");
    Settings audioValue = value;
    audioValue.audioSetup = AudioSetupPreference{AudioBackendKind::asio, "Test driver", "Test driver", 48000, 128, 2, 2, {4, 5}, {6, 7}};
    require(store.save(audioValue).succeeded && store.load().value == audioValue,
            "Machine-local backend/device-name/rate/buffer/channel hints round-trip without tokens or transport state.");
    for (const auto backend : {AudioBackendKind::asio, AudioBackendKind::wasapiExclusive, AudioBackendKind::wasapiShared,
                               AudioBackendKind::coreAudio, AudioBackendKind::directSound})
    {
        audioValue.audioSetup->backend = backend;
        require(store.save(audioValue).succeeded && store.load().value == audioValue,
                "All stable numerical backend values round-trip, including CoreAudio=3 and DirectSound=4.");
    }
    auto file = directory.file.getChildFile("settings.json");
    require(file.replaceWithText(R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{"backend":3,"inputName":"","outputName":"Legacy device","sampleRate":48000,"bufferFrames":128,"inputChannels":0,"outputChannels":4}})"),
            "Seed pre-port-selection preferences.");
    auto migrated = store.load();
    require(migrated.state == SettingsLoadState::loaded && migrated.value.audioSetup->backend == AudioBackendKind::coreAudio
            && migrated.value.audioSetup->inputChannelIndices.empty()
            && migrated.value.audioSetup->outputChannelIndices == std::vector<int>{0, 1, 2, 3},
            "Legacy counts migrate explicitly to first-N indices without changing CoreAudio's numeric identity.");
    require(store.save(migrated.value).succeeded && store.load().value == migrated.value
            && file.loadFileAsString().contains("outputChannelIndices"), "Migration persists explicit channel identities on the next save.");
    require(store.save(value).succeeded, "Optional audio preferences remain compatible with window-only settings.");
    const auto original = file.loadFileAsString();
    require(!store.save({1, 1, true}).succeeded && file.loadFileAsString() == original,
            "Invalid save preserves original.");
    for (const auto* invalid : {
             "{", R"({"version":99})",
             R"({"version":1,"windowWidth":"1200","windowHeight":800,"inspectorVisible":true})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":1})",
             R"({"version":1,"windowWidth":1200,"windowHeight":1,"inspectorVisible":true})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{}})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{"backend":4,"inputName":"","outputName":"Device","sampleRate":48000,"bufferFrames":128,"inputChannels":0,"outputChannels":2,"outputChannelIndices":[2,2]}})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{"backend":4,"inputName":"","outputName":"Device","sampleRate":48000,"bufferFrames":128,"inputChannels":0,"outputChannels":2,"outputChannelIndices":[]}})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{"backend":4,"inputName":"","outputName":"Device","sampleRate":48000,"bufferFrames":128,"inputChannels":0,"outputChannels":2,"outputChannelIndices":[0,"1"]}})",
             R"({"version":1,"windowWidth":1200,"windowHeight":800,"inspectorVisible":true,"audioSetup":{"backend":99,"inputName":"","outputName":"Device","sampleRate":48000,"bufferFrames":128,"inputChannels":0,"outputChannels":2}})"})
    {
        require(file.replaceWithText(invalid), "Seed invalid settings.");
        require(store.load().state == SettingsLoadState::failed, "Malformed/types/version/ranges fail explicitly.");
        require(!store.save(value).succeeded && file.loadFileAsString() == invalid,
                "Unreadable/unsupported content is not overwritten.");
    }
    require(file.replaceWithText(original), "Restore supported preferences.");
    require(store.save({1400, 900, true}).succeeded, "Replace existing settings.");
    val::infrastructure::LocalSettingsStore unicode(
        directory.path() / std::filesystem::path(u8"settings-\u00e9-\u97f3.json"));
    require(unicode.save(value).succeeded && unicode.load().value == value,
            "Native Unicode settings paths round-trip through the adapter.");
    require(file.replaceWithText(juce::String::repeatedString(" ", 65537)), "Seed oversized settings.");
    require(store.load().state == SettingsLoadState::failed && !store.save(value).succeeded,
            "Oversized existing settings are rejected and preserved.");
    val::infrastructure::LocalSettingsStore directoryStore(directory.path());
    require(directoryStore.load().state == SettingsLoadState::failed, "Directory is not a settings file.");
    auto blocker = directory.file.getChildFile("not-a-directory");
    require(blocker.replaceWithText("blocker"), "Create blocked parent.");
    val::infrastructure::LocalSettingsStore blocked(directory.path() / "not-a-directory" / "settings.json");
    require(!blocked.save(value).succeeded, "Invalid destination fails visibly.");
}

void testLogger(const TestDirectory& directory)
{
    using namespace val::application;
    const auto path = directory.path() / "Logs" / "test.log";
    val::infrastructure::FileLogger logger(path, 4096);
    require(logger.write(LogLevel::info, "test\ncategory", "hello\nworld").succeeded, "Write diagnostic.");
    auto file = directory.file.getChildFile("Logs/test.log");
    const auto text = file.loadFileAsString();
    require(text.contains("test category hello world"), "Control characters cannot forge extra log lines.");
    for (int i = 0; i < 10; ++i)
        require(logger.write(LogLevel::warning, "test", std::string(2000, 'x')).succeeded, "Bounded rotation succeeds.");
    require(file.getSize() <= 4096
            && directory.file.getChildFile("Logs/test.log.previous").getSize() <= 4096,
            "Current and previous log files remain bounded.");
    bool firstOk{}, secondOk{};
    std::thread first([&] { firstOk = logger.write(LogLevel::info, "test", "first").succeeded; });
    std::thread second([&] { secondOk = logger.write(LogLevel::info, "test", "second").succeeded; });
    first.join();
    second.join();
    require(firstOk && secondOk, "Parallel ordinary writes serialize.");
    val::infrastructure::FileLogger invalid(directory.path());
    require(!invalid.write(LogLevel::error, "test", "failure").succeeded, "Invalid log destination reports failure.");
}
}

int main()
{
    return runTests([]
    {
        const TestDirectory directory;
        testSettings(directory);
        testLogger(directory);
    });
}
