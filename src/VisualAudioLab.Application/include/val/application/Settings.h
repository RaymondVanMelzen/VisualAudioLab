#pragma once

#include "Result.h"

namespace val::application
{
/** Machine-local window preferences, separate from portable project content. */
struct Settings final
{
    int windowWidth{1100};
    int windowHeight{720};
    bool inspectorVisible{true};
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
