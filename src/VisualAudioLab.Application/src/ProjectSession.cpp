#include <val/application/ProjectSession.h>
#include <algorithm>

namespace val::application
{
namespace
{
std::filesystem::path normalise(const std::filesystem::path& path)
{
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : resolved;
}

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b)
{
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error))
        return true;
    return normalise(a) == normalise(b);
}
}

ProjectSession::ProjectSession(WorkspaceService& workspace, ProjectStore& store)
    : workspace_(workspace), store_(store), saved_(workspace.project())
{
    if (auto result = store_.readRecent(recent_); !result.succeeded)
        warning_ = result.error;
    std::vector<std::filesystem::path> unique;
    for (const auto& path : recent_)
        if (!path.empty() && std::none_of(unique.begin(), unique.end(), [&](const auto& item) { return samePath(item, path); })
            && unique.size() < 10)
            unique.push_back(normalise(path));
    recent_.swap(unique);
}

bool ProjectSession::dirty() const
{
    const auto& current = workspace_.project();
    if (current.workspace != saved_.workspace || current.patches.size() != saved_.patches.size())
        return true;
    for (const auto& [id, patch] : current.patches)
    {
        const auto found = saved_.patches.find(id);
        if (found == saved_.patches.end() || patch.name != found->second.name || patch.graph != found->second.graph)
            return true;
    }
    return false;
}

Result ProjectSession::remember(const std::filesystem::path& path)
{
    auto next = recent_;
    std::erase_if(next, [&](const auto& item) { return samePath(item, path); });
    next.insert(next.begin(), normalise(path));
    if (next.size() > 10)
        next.resize(10);
    const auto result = store_.writeRecent(next);
    if (result.succeeded)
        recent_.swap(next);
    return result;
}

Result ProjectSession::forgetRecent(const std::filesystem::path& path)
{
    auto next = recent_;
    std::erase_if(next, [&](const auto& item) { return samePath(item, path); });
    const auto result = store_.writeRecent(next);
    if (result.succeeded)
        recent_.swap(next);
    return result;
}

Result ProjectSession::load(const std::filesystem::path& path)
{
    if (path.empty())
        return Result::failure("Choose a .valab project to load.");
    auto loaded = store_.read(path);
    if (!loaded.file)
        return Result::failure(loaded.error);
    auto saved = loaded.file->state;
    auto templateSource = loaded.file->source;
    auto nextPath = normalise(path);
    const auto result = workspace_.replaceProject(std::move(loaded.file->state));
    if (!result.succeeded)
        return result;
    saved_ = std::move(saved);
    source_ = std::move(loaded.file->source);
    templateSource_.swap(templateSource);
    path_.swap(nextPath);
    const auto recentResult = remember(path_);
    warning_ = recentResult.succeeded ? "" : recentResult.error;
    return Result::success();
}

Result ProjectSession::save() { return saveAs(path_); }

Result ProjectSession::saveAs(const std::filesystem::path& path)
{
    if (path.empty())
        return Result::failure("Choose Save As to give this project a .valab location.");
    auto nextPath = normalise(path);
    ProjectFile prepared{workspace_.project(), templateSource_};
    const auto validation = validateProject(prepared.state);
    if (!validation.succeeded)
        return validation;
    const auto result = store_.write(nextPath, prepared,
        !path_.empty() && samePath(path_, nextPath) ? std::optional(source_) : std::nullopt);
    if (!result.succeeded)
        return result;
    saved_ = std::move(prepared.state);
    source_ = std::move(prepared.source);
    if (templateSource_.empty())
        templateSource_ = source_;
    path_.swap(nextPath);
    const auto recentResult = remember(path_);
    warning_ = recentResult.succeeded ? "" : recentResult.error;
    return Result::success();
}
}
