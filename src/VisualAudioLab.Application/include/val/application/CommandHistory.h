#pragma once

#include "Command.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace val::application
{
/**
 * Single-owner, single-thread undo/redo journal with a bounded entry count.
 * Failed operations preserve history and redo branches. A successful new command truncates
 * redo; oldest entries are discarded at capacity. Not event sourcing or persisted history.
 */
class CommandHistory final
{
public:
    explicit CommandHistory(std::size_t capacity = 128);
    [[nodiscard]] Result execute(std::unique_ptr<Command> command);
    [[nodiscard]] Result undo();
    [[nodiscard]] Result redo();
    void clear() noexcept;
    [[nodiscard]] bool canUndo() const noexcept { return cursor_ != 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < entries_.size(); }
    [[nodiscard]] std::string_view undoName() const noexcept;
    [[nodiscard]] std::string_view redoName() const noexcept;

private:
    const std::size_t capacity_;
    std::vector<std::unique_ptr<Command>> entries_;
    std::size_t cursor_{};
};
}
