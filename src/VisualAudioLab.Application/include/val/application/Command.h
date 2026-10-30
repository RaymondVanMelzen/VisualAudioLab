#pragma once

#include "Result.h"
#include <string_view>

namespace val::application
{
/**
 * One reversible, UI/control-thread operation, owned by its document history.
 * execute() also performs redo. On failure or exception, implementations must leave their
 * target unchanged. undo() obeys the same atomicity contract. Targets outlive history entries.
 * Commands must not re-enter their history, perform I/O, or control audio.
 */
class Command
{
public:
    virtual ~Command() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Result execute() = 0;
    [[nodiscard]] virtual Result undo() = 0;
};
}
