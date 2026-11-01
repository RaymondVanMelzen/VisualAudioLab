#pragma once

#include "Result.h"
#include "IAudioBackend.h"
#include <cmath>

namespace val::application
{
/** Name-based machine-local setup hint, always rediscovered and inspected before use. No tokens or transport state. */
struct AudioSetupPreference final
{
    AudioBackendKind backend{};
    std::string inputName, outputName;
    double sampleRate{};
    int bufferFrames{}, inputChannels{}, outputChannels{};
    std::vector<int> inputChannelIndices, outputChannelIndices;
    bool operator==(const AudioSetupPreference&) const = default;
};

/** Machine-local preferences, separate from portable project content. */
struct Settings final
{
    int windowWidth{1100};
    int windowHeight{720};
    bool inspectorVisible{true};
    std::optional<AudioSetupPreference> audioSetup;
    bool operator==(const Settings&) const = default;
};

/** Distinguishes an absent settings file from successful loading or malformed/unreadable data. */
enum class SettingsLoadState { loaded, missing, failed };

/** Value snapshot with explicit load provenance; failure never supplies accepted settings. */
struct SettingsLoadResult final
{
    SettingsLoadState state;
    Settings value;
    std::string error;
};

/** Validates the shell's supported preference ranges before accepting or persisting them. */
[[nodiscard]] inline Result validateSettings(const Settings& settings)
{
    if (settings.windowWidth < 800 || settings.windowWidth > 7680
        || settings.windowHeight < 600 || settings.windowHeight > 4320)
        return Result::failure("Window size must be within 800x600 and 7680x4320.");
    if (settings.audioSetup)
    {
        const auto& audio = *settings.audioSetup;
        if (audio.backend < AudioBackendKind::asio || audio.backend > AudioBackendKind::directSound
            || audio.outputName.empty() || audio.outputName.size() > 4096 || audio.inputName.size() > 4096
            || !std::isfinite(audio.sampleRate) || audio.sampleRate < 8000 || audio.sampleRate > 384000
            || audio.bufferFrames < 1 || audio.bufferFrames > 4096 || audio.inputChannels < 0
            || audio.outputChannels < 1 || audio.outputChannels > 32
            || (audio.inputChannels != 0 && audio.inputChannels != audio.outputChannels)
            || (audio.inputChannels > 0) != !audio.inputName.empty()
            || (audio.backend == AudioBackendKind::asio && !audio.inputName.empty() && audio.inputName != audio.outputName))
            return Result::failure("Audio setup preferences have invalid backend, device names, rate, buffer or channel counts.");
        if ((!audio.inputChannelIndices.empty() && !validAudioChannelSelection(audio.inputChannelIndices, audio.inputChannels))
            || (!audio.outputChannelIndices.empty() && !validAudioChannelSelection(audio.outputChannelIndices, audio.outputChannels)))
            return Result::failure("Audio setup channel identities must be ordered, unique and match the saved width.");
    }
    return Result::success();
}

/** Local persistence port; adapters preserve originals on failed or unsupported loads/saves. */
class SettingsStore
{
public:
    virtual ~SettingsStore() = default;
    [[nodiscard]] virtual SettingsLoadResult load() = 0;
    [[nodiscard]] virtual Result save(const Settings& settings) = 0;
};
}
