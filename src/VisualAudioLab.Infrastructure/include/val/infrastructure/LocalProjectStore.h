#pragma once

#include <val/application/Project.h>

namespace val::infrastructure
{
/**
 * Readable versioned JSON adapter with unknown-field merging and same-directory replacement.
 * The optional recent-list location is separate machine-local metadata, never project content.
 * Unsupported/corrupt targets and externally changed active files are not overwritten.
 */
class LocalProjectStore final : public application::ProjectStore
{
public:
    explicit LocalProjectStore(std::filesystem::path recentFile) : recentFile_(std::move(recentFile)) {}
    application::ProjectReadResult read(const std::filesystem::path& path) override;
    application::Result write(const std::filesystem::path& path, application::ProjectFile& file,
                              const std::optional<std::string>& expectedSource) override;
    application::Result readRecent(std::vector<std::filesystem::path>& paths) override;
    application::Result writeRecent(const std::vector<std::filesystem::path>& paths) override;
    /** Codec entry points share exactly the validation used by disk operations. */
    [[nodiscard]] static application::ProjectReadResult decode(const std::string& text);
    [[nodiscard]] static application::Result encode(application::ProjectFile& file);

private:
    std::filesystem::path recentFile_;
};
}
