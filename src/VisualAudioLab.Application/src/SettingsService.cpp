#include <val/application/SettingsService.h>

namespace val::application
{
Result SettingsService::load()
{
    mayWrite_ = false;
    const auto loaded = store_.load();
    if (loaded.state == SettingsLoadState::failed)
        return Result::failure(loaded.error);

    const auto candidate = loaded.state == SettingsLoadState::missing ? Settings{} : loaded.value;
    auto result = validateSettings(candidate);
    if (!result.succeeded)
        return result;
    current_ = candidate;
    mayWrite_ = true;
    return Result::success();
}

Result SettingsService::save(const Settings& settings)
{
    if (!mayWrite_)
        return Result::failure("Settings writes are disabled until preferences have been loaded successfully.");
    auto result = validateSettings(settings);
    if (!result.succeeded)
        return result;
    result = store_.save(settings);
    if (result.succeeded)
        current_ = settings;
    return result;
}
}
