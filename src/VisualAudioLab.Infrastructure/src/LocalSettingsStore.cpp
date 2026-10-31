#include <val/infrastructure/LocalSettingsStore.h>
#include <juce_core/juce_core.h>
#include <optional>

namespace val::infrastructure
{
namespace
{
std::optional<juce::File> asJuceFile(const std::filesystem::path& path)
{
    if (path.empty())
        return std::nullopt;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error)
        return std::nullopt;
    const auto utf8 = absolute.u8string();
    return juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()),
                                            static_cast<int>(utf8.size())));
}

application::SettingsLoadResult loadFailure(std::string error)
{
    return {application::SettingsLoadState::failed, {}, std::move(error)};
}
}

application::SettingsLoadResult LocalSettingsStore::load()
{
    const auto resolved = asJuceFile(file_);
    if (!resolved)
        return loadFailure("Cannot resolve the settings location.");
    std::error_code error;
    const auto exists = std::filesystem::exists(file_, error);
    if (error)
        return loadFailure("Cannot inspect the settings location.");
    if (!exists)
        return {application::SettingsLoadState::missing, {}, {}};
    const auto& file = *resolved;
    if (!file.existsAsFile() || file.getSize() > 64 * 1024)
        return loadFailure("Settings must be a regular file no larger than 64 KiB.");
    juce::FileInputStream input(file);
    if (!input.openedOk())
        return loadFailure("Cannot open the local settings file.");
    const auto text = input.readEntireStreamAsString();
    if (input.getStatus().failed())
        return loadFailure("Cannot read the local settings file.");
    juce::var parsed;
    if (juce::JSON::parse(text, parsed).failed() || !parsed.isObject())
        return loadFailure("Settings contain invalid JSON.");

    auto* object = parsed.getDynamicObject();
    const auto& version = object->getProperty("version");
    const auto& width = object->getProperty("windowWidth");
    const auto& height = object->getProperty("windowHeight");
    const auto& inspector = object->getProperty("inspectorVisible");
    if (!version.isInt() || static_cast<int>(version) != 1)
        return loadFailure("Settings schema is missing or unsupported; the original is preserved.");
    if (!width.isInt() || !height.isInt() || !inspector.isBool())
        return loadFailure("Settings have missing or invalid preference types.");
    application::Settings settings{static_cast<int>(width), static_cast<int>(height),
                                    static_cast<bool>(inspector)};
    if (object->hasProperty("audioSetup"))
    {
        auto* audio = object->getProperty("audioSetup").getDynamicObject();
        if (!audio || !audio->getProperty("backend").isInt()
            || !audio->getProperty("inputName").isString() || !audio->getProperty("outputName").isString()
            || !(audio->getProperty("sampleRate").isDouble() || audio->getProperty("sampleRate").isInt())
            || !audio->getProperty("bufferFrames").isInt() || !audio->getProperty("inputChannels").isInt()
            || !audio->getProperty("outputChannels").isInt())
            return loadFailure("Audio setup preferences have missing or invalid types; original preserved.");
        const auto backend = static_cast<int>(audio->getProperty("backend"));
        if (backend < 0 || backend > 4) return loadFailure("Audio setup backend is unsupported; original preserved.");
        settings.audioSetup = application::AudioSetupPreference{
            static_cast<application::AudioBackendKind>(backend),
            audio->getProperty("inputName").toString().toStdString(), audio->getProperty("outputName").toString().toStdString(),
            static_cast<double>(audio->getProperty("sampleRate")), static_cast<int>(audio->getProperty("bufferFrames")),
            static_cast<int>(audio->getProperty("inputChannels")), static_cast<int>(audio->getProperty("outputChannels"))};
        auto& setup = *settings.audioSetup;
        const auto readIndices = [&](const char* key, int count, std::vector<int>& indices)
        {
            if (!audio->hasProperty(key)) { indices = application::legacyAudioChannels(count); return true; }
            const auto* array = audio->getProperty(key).getArray();
            if (!array) return false;
            for (const auto& index : *array)
            {
                if (!index.isInt()) return false;
                indices.push_back(static_cast<int>(index));
            }
            return application::validAudioChannelSelection(indices, count);
        };
        if (!readIndices("inputChannelIndices", setup.inputChannels, setup.inputChannelIndices)
            || !readIndices("outputChannelIndices", setup.outputChannels, setup.outputChannelIndices))
            return loadFailure("Audio setup contains invalid channel identities; original preserved.");
    }
    const auto validation = application::validateSettings(settings);
    if (!validation.succeeded)
        return loadFailure(validation.error);
    return {application::SettingsLoadState::loaded, settings, {}};
}

application::Result LocalSettingsStore::save(const application::Settings& settings)
{
    auto validation = application::validateSettings(settings);
    if (!validation.succeeded)
        return validation;
    const auto existing = load();
    if (existing.state == application::SettingsLoadState::failed)
        return application::Result::failure(existing.error);

    const auto resolved = asJuceFile(file_);
    if (!resolved)
        return application::Result::failure("Cannot resolve the settings location.");
    const auto& file = *resolved;
    if (file.getParentDirectory().createDirectory().failed())
        return application::Result::failure("Cannot create the local settings directory.");

    auto* object = new juce::DynamicObject();
    const juce::var data(object);
    object->setProperty("version", 1);
    object->setProperty("windowWidth", settings.windowWidth);
    object->setProperty("windowHeight", settings.windowHeight);
    object->setProperty("inspectorVisible", settings.inspectorVisible);
    if (settings.audioSetup)
    {
        auto* audio = new juce::DynamicObject();
        const auto& setup = *settings.audioSetup;
        audio->setProperty("backend", static_cast<int>(setup.backend));
        audio->setProperty("inputName", juce::String::fromUTF8(setup.inputName.c_str()));
        audio->setProperty("outputName", juce::String::fromUTF8(setup.outputName.c_str()));
        audio->setProperty("sampleRate", setup.sampleRate);
        audio->setProperty("bufferFrames", setup.bufferFrames);
        audio->setProperty("inputChannels", setup.inputChannels);
        audio->setProperty("outputChannels", setup.outputChannels);
        const auto writeIndices = [&](const char* key, const std::vector<int>& indices, int count)
        {
            juce::Array<juce::var> values;
            for (int index : indices.empty() ? application::legacyAudioChannels(count) : indices) values.add(index);
            audio->setProperty(key, values);
        };
        writeIndices("inputChannelIndices", setup.inputChannelIndices, setup.inputChannels);
        writeIndices("outputChannelIndices", setup.outputChannelIndices, setup.outputChannels);
        object->setProperty("audioSetup", juce::var(audio));
    }
    const juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data), false, false, "\n"))
        return application::Result::failure("Cannot write temporary settings; the original is preserved.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return application::Result::failure("Cannot replace settings with the prepared temporary file.");
    return application::Result::success();
}
}
