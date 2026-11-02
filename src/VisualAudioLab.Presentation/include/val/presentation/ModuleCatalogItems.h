#pragma once

#include <array>
#include <string_view>

namespace val::presentation
{
enum class ModuleCategory : int
{
    all,
    effects,
    analysis,
    routing,
    customDsp
};

struct ModuleCatalogEntry
{
    std::string_view name;
    ModuleCategory category;
    std::string_view description;
};

inline constexpr std::array<ModuleCatalogEntry, 15> moduleCatalog{
    ModuleCatalogEntry{"Input", ModuleCategory::routing, "External audio input routing utility."},
    ModuleCatalogEntry{"Output", ModuleCategory::routing, "Audio output routing utility."},
    ModuleCatalogEntry{"Gain", ModuleCategory::effects, "Adjusts linear signal level."},
    ModuleCatalogEntry{"EQ", ModuleCategory::effects, "Shapes frequency response."},
    ModuleCatalogEntry{"Delay", ModuleCategory::effects, "Adds a short delayed signal."},
    ModuleCatalogEntry{"Compressor", ModuleCategory::effects, "Controls dynamic range."},
    ModuleCatalogEntry{"Distortion", ModuleCategory::effects, "Adds nonlinear harmonics."},
    ModuleCatalogEntry{"Chorus", ModuleCategory::effects, "Adds modulated delay."},
    ModuleCatalogEntry{"Reverb", ModuleCategory::effects, "Adds a reverberant tail."},
    ModuleCatalogEntry{"Oscilloscope", ModuleCategory::analysis, "Displays signal over time."},
    ModuleCatalogEntry{"Spectrum Analyzer", ModuleCategory::analysis, "Displays frequency content."},
    ModuleCatalogEntry{"Peak Meter", ModuleCategory::analysis, "Displays peak level."},
    ModuleCatalogEntry{"RMS Meter", ModuleCategory::analysis, "Displays average level."},
    ModuleCatalogEntry{"Guitar Tuner", ModuleCategory::analysis, "Tunes a connected guitar signal to equal-tempered notes."},
    ModuleCatalogEntry{"Script", ModuleCategory::customDsp, "User-authored audio function; may define a source or effect."}};

inline constexpr std::array<std::string_view, 5> moduleCategoryNames{
    "All modules", "Effects", "Analysis", "Routing utilities", "Custom DSP"};

[[nodiscard]] constexpr bool isModuleCatalogType(std::string_view type) noexcept
{
    for (const auto& entry : moduleCatalog)
        if (entry.name == type)
            return true;
    return false;
}
}
