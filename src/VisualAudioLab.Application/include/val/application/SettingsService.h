#pragma once

#include "Settings.h"

namespace val::application
{
/**
 * Validated settings lifecycle with explicit error propagation.
 * Failed loads preserve the current value and inhibit writes until a later successful/missing
 * load, protecting unsupported or unreadable files from automatic shutdown overwrites.
 * The injected store outlives this single-thread service.
 */
class SettingsService final
{
public:
    explicit SettingsService(SettingsStore& store) : store_(store) {}
    [[nodiscard]] Result load();
    [[nodiscard]] Result save(const Settings& settings);
    [[nodiscard]] const Settings& current() const noexcept { return current_; }

private:
    SettingsStore& store_;
    Settings current_;
    bool mayWrite_{false};
};
}
