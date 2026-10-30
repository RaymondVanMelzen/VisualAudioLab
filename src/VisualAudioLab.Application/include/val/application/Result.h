#pragma once

#include <string>
#include <utility>

namespace val::application
{
/** Explicit operation outcome; callers must surface errors rather than infer success. */
struct Result final
{
    bool succeeded;
    std::string error;

    [[nodiscard]] static Result success() { return {true, {}}; }
    [[nodiscard]] static Result failure(std::string message) { return {false, std::move(message)}; }
};
}
