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
    const juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data), false, false, "\n"))
        return application::Result::failure("Cannot write temporary settings; the original is preserved.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return application::Result::failure("Cannot replace settings with the prepared temporary file.");
    return application::Result::success();
}
}
