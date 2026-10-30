#pragma once

#include <val/application/Settings.h>
#include <filesystem>

namespace val::infrastructure
{
/**
 * Explicit-path, versioned JSON preferences adapter using JUCE only inside its implementation.
 * Validates schema/types/ranges before returning data. Writes through a sibling temporary
 * file; invalid existing data is not overwritten. Single-process/control-thread use only.
 * This stores application window preferences, never projects, devices, scripts or secrets.
 */
class LocalSettingsStore final : public application::SettingsStore
{
public:
    explicit LocalSettingsStore(std::filesystem::path file) : file_(std::move(file)) {}
    [[nodiscard]] application::SettingsLoadResult load() override;
    [[nodiscard]] application::Result save(const application::Settings& settings) override;

private:
    std::filesystem::path file_;
};
}
