#pragma once

#include <string>

namespace val::domain
{
/** Non-persisted workspace identity; deliberately contains no patch or processing state. */
struct Workspace final
{
    std::string name{"Untitled"};
    bool operator==(const Workspace&) const = default;
};
}
