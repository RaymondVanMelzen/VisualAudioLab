#pragma once

#include <val/application/Logger.h>
#include <cstdint>
#include <filesystem>
#include <mutex>

namespace val::infrastructure
{
/**
 * Synchronous, thread-serialized local text sink with a bounded current/previous file pair.
 * The owner supplies a non-project path. Records escape control characters and truncate
 * unbounded fields. Write/rotation errors propagate explicitly; there is no remote sink.
 * This service performs I/O and must never be reached from realtime code.
 */
class FileLogger final : public application::Logger
{
public:
    explicit FileLogger(std::filesystem::path file, std::uintmax_t maximumBytes = 1024 * 1024);
    [[nodiscard]] application::Result write(application::LogLevel level, std::string_view category,
                                             std::string_view message) override;

private:
    std::filesystem::path file_;
    std::uintmax_t maximumBytes_;
    std::mutex mutex_;
};
}
