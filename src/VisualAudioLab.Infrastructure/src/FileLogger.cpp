#include <val/infrastructure/FileLogger.h>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace val::infrastructure
{
namespace
{
std::string safeField(std::string_view text, std::size_t limit)
{
    std::string output;
    for (const auto ch : text.substr(0, limit))
        output.push_back(static_cast<unsigned char>(ch) < 32 || ch == 127 ? ' ' : ch);
    if (text.size() > limit)
        output += "...";
    return output;
}

std::string_view levelName(application::LogLevel level)
{
    switch (level)
    {
        case application::LogLevel::info: return "INFO";
        case application::LogLevel::warning: return "WARN";
        case application::LogLevel::error: return "ERROR";
    }
    return "UNKNOWN";
}
}

FileLogger::FileLogger(std::filesystem::path file, std::uintmax_t maximumBytes)
    : file_(std::move(file)), maximumBytes_(maximumBytes)
{
    if (maximumBytes_ < 4096)
        throw std::invalid_argument("Log capacity must be at least 4096 bytes.");
}

application::Result FileLogger::write(application::LogLevel level, std::string_view category,
                                       std::string_view message)
{
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto record = std::to_string(timestamp) + " " + std::string(levelName(level)) + " "
        + safeField(category, 128) + " " + safeField(message, 2048) + "\n";

    const std::lock_guard lock(mutex_);
    std::error_code error;
    if (!file_.parent_path().empty())
        std::filesystem::create_directories(file_.parent_path(), error);
    if (error)
        return application::Result::failure("Cannot create the local log directory.");

    const auto exists = std::filesystem::exists(file_, error);
    if (error)
        return application::Result::failure("Cannot inspect the local log file.");
    if (exists)
    {
        const auto size = std::filesystem::file_size(file_, error);
        if (error)
            return application::Result::failure("Cannot read the local log size.");
        if (size + record.size() > maximumBytes_)
        {
            auto previous = file_;
            previous += ".previous";
            std::filesystem::remove(previous, error);
            if (error)
                return application::Result::failure("Cannot rotate the previous local log.");
            std::filesystem::rename(file_, previous, error);
            if (error)
                return application::Result::failure("Cannot rotate the current local log.");
        }
    }

    std::ofstream stream(file_, std::ios::binary | std::ios::app);
    stream << record;
    stream.flush();
    if (!stream)
        return application::Result::failure("Cannot write the local log.");
    return application::Result::success();
}
}
