#pragma once

#include "Result.h"
#include <string_view>

namespace val::application
{
/** Ordinary local diagnostic severity, never a render-thread transport. */
enum class LogLevel { info, warning, error };

/**
 * Application-owned synchronous diagnostics boundary.
 * The composition root owns the sink; callers must not log content/secrets or use it from realtime work.
 * A failed write is returned explicitly and must not recursively log itself.
 */
class Logger
{
public:
    virtual ~Logger() = default;
    [[nodiscard]] virtual Result write(LogLevel level, std::string_view category,
                                       std::string_view message) = 0;
};
}
