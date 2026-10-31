#include <val/application/WorkspaceService.h>
#include <algorithm>
#include <utility>

namespace val::application
{
namespace
{
/** Exchanges prepared names without allocating during execute/undo; workspace outlives history. */
class RenameWorkspace final : public Command
{
public:
    RenameWorkspace(domain::Workspace& workspace, std::string replacement)
        : workspace_(workspace), other_(std::move(replacement)) {}
    std::string_view name() const noexcept override { return "Rename workspace"; }
    Result execute() override { workspace_.name.swap(other_); return Result::success(); }
    Result undo() override { workspace_.name.swap(other_); return Result::success(); }

private:
    domain::Workspace& workspace_;
    std::string other_;
};
}

Result WorkspaceService::rename(std::string name)
{
    if (name.empty() || name.size() > 128
        || std::all_of(name.begin(), name.end(), [](unsigned char ch) { return ch <= 32; })
        || std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }))
        return Result::failure("Use a non-blank workspace name of at most 128 UTF-8 bytes without control characters.");
    if (name == state_.workspace.name)
        return Result::success();
    return history_.execute(std::make_unique<RenameWorkspace>(state_.workspace, std::move(name)));
}
}
