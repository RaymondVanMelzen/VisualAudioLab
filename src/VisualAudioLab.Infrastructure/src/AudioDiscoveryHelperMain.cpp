#include <val/infrastructure/AudioBackends.h>
#include <juce_events/juce_events.h>

namespace
{
int run(const juce::StringArray& arguments)
{
    if (arguments.size() != 2) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File input(arguments[0]);
    const juce::File output(arguments[1]);
    if (!input.existsAsFile() || input.getSize() > 1024 * 1024) return 2;
    int status = 3;
    juce::MessageManager::callAsync([&]
    {
        const auto result = val::infrastructure::runAudioDiscoveryHelper(input.loadFileAsString().toStdString());
        status = output.replaceWithText(juce::String::fromUTF8(result.c_str())) ? 0 : 3;
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    return status;
}
}
#if JUCE_WINDOWS
int wmain(int argc, wchar_t** argv)
{
    juce::StringArray arguments;
    for (int i = 1; i < argc; ++i) arguments.add(juce::String(argv[i]));
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    juce::StringArray arguments;
    for (int i = 1; i < argc; ++i) arguments.add(juce::String::fromUTF8(argv[i]));
    return run(arguments);
}
#endif
