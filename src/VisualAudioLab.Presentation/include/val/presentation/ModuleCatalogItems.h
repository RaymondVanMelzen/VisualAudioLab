#pragma once

#include <array>
#include <string_view>

namespace val::presentation
{
enum class ModuleCategory : int
{
    all,
    effects,
    generators,
    analysis,
    routing,
};

struct ModuleCatalogEntry
{
    std::string_view name;
    ModuleCategory category;
    std::string_view description;
};

inline constexpr std::array<ModuleCatalogEntry, 21> moduleCatalog{
    ModuleCatalogEntry{"Sum", ModuleCategory::routing, "Explicit unity sum of corresponding channels; may exceed full scale. No limiter."},
    ModuleCatalogEntry{"Gain", ModuleCategory::effects, "Adjusts linear signal level."},
    ModuleCatalogEntry{"EQ", ModuleCategory::effects, "Shapes frequency response."},
    ModuleCatalogEntry{"Delay", ModuleCategory::effects, "Adds a short delayed signal."},
    ModuleCatalogEntry{"Compressor", ModuleCategory::effects, "Controls dynamic range."},
    ModuleCatalogEntry{"Distortion", ModuleCategory::effects, "Adds nonlinear harmonics."},
    ModuleCatalogEntry{"Chorus", ModuleCategory::effects, "Adds modulated delay."},
    ModuleCatalogEntry{"Reverb", ModuleCategory::effects, "Adds a reverberant tail."},
    ModuleCatalogEntry{"Signal Generator", ModuleCategory::generators, "Produces selectable test waveforms, noise, DC or a frequency sweep."},
    ModuleCatalogEntry{"Impulse Generator", ModuleCategory::generators, "Emits a one-sample impulse for filter and tail testing."},
    ModuleCatalogEntry{"Click Generator", ModuleCategory::generators, "Produces a quiet BPM-synchronized transient."},
    ModuleCatalogEntry{"Burst Generator", ModuleCategory::generators, "Produces a finite, enveloped tone burst."},
    ModuleCatalogEntry{"Chirp Generator", ModuleCategory::generators, "Produces a linear, logarithmic or curved swept sine."},
    ModuleCatalogEntry{"Noise Generator", ModuleCategory::generators, "Produces deterministic white or filtered pink noise."},
    ModuleCatalogEntry{"DC Generator", ModuleCategory::generators, "Produces an exact constant value in the range -1 to +1."},
    ModuleCatalogEntry{"Oscilloscope", ModuleCategory::analysis, "Displays signal over time."},
    ModuleCatalogEntry{"Spectrum Analyzer", ModuleCategory::analysis, "Displays frequency content."},
    ModuleCatalogEntry{"Peak Meter", ModuleCategory::analysis, "Displays peak level."},
    ModuleCatalogEntry{"RMS Meter", ModuleCategory::analysis, "Displays average level."},
    ModuleCatalogEntry{"Correlation Meter", ModuleCategory::analysis, "Measures mean-removed stereo correlation over the RMS window."},
    ModuleCatalogEntry{"Guitar Tuner", ModuleCategory::analysis, "Tunes a connected guitar signal to equal-tempered notes."}};

inline constexpr std::array<std::string_view, 5> moduleCategoryNames{
    "All modules", "Effects", "Generators", "Analysis", "Routing utilities"};

[[nodiscard]] constexpr bool isModuleCatalogType(std::string_view type) noexcept
{
    for (const auto& entry : moduleCatalog)
        if (entry.name == type)
            return true;
    return false;
}
}
