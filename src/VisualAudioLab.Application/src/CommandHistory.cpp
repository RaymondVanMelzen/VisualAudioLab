#include <val/application/CommandHistory.h>
#include <stdexcept>

namespace val::application
{
CommandHistory::CommandHistory(std::size_t capacity) : capacity_(capacity)
{
    if (capacity == 0)
        throw std::invalid_argument("History capacity must be positive.");
    entries_.reserve(capacity_);
}

Result CommandHistory::execute(std::unique_ptr<Command> command)
{
    if (!command)
        return Result::failure("Cannot execute an empty command.");

    auto result = command->execute();
    if (!result.succeeded)
        return result;

    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
    if (entries_.size() == capacity_)
        entries_.erase(entries_.begin());
    entries_.push_back(std::move(command));
    cursor_ = entries_.size();
    return result;
}

Result CommandHistory::undo()
{
    if (!canUndo())
        return Result::failure("Nothing to undo.");
    auto result = entries_[cursor_ - 1]->undo();
    if (result.succeeded)
        --cursor_;
    return result;
}

Result CommandHistory::redo()
{
    if (!canRedo())
        return Result::failure("Nothing to redo.");
    auto result = entries_[cursor_]->execute();
    if (result.succeeded)
        ++cursor_;
    return result;
}

void CommandHistory::clear() noexcept
{
    entries_.clear();
    cursor_ = 0;
}

std::string_view CommandHistory::undoName() const noexcept
{
    return canUndo() ? entries_[cursor_ - 1]->name() : std::string_view{};
}

std::string_view CommandHistory::redoName() const noexcept
{
    return canRedo() ? entries_[cursor_]->name() : std::string_view{};
}
}
