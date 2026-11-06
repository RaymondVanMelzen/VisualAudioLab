#pragma once

#include "Graph.h"
#include <string_view>

namespace val::domain
{
[[nodiscard]] inline std::string_view audioType(const Node& node) noexcept
{
    const auto field = node.parameters.find("audio.type");
    const auto* type = field == node.parameters.end() ? nullptr : std::get_if<std::string>(&field->second);
    return type ? std::string_view(*type) : std::string_view{};
}
[[nodiscard]] inline bool isSumPort(const Node& node) noexcept { return audioType(node) == "Sum"; }
[[nodiscard]] inline bool hasInputPort(const Node& node) noexcept
{
    const auto type = audioType(node);
    return type != "Input" && type != "Signal Generator" && type != "Impulse Generator"
        && type != "Click Generator" && type != "Burst Generator" && type != "Chirp Generator"
        && type != "Noise Generator" && type != "DC Generator";
}
[[nodiscard]] inline bool hasOutputPort(const Node& node) noexcept { return audioType(node) != "Output"; }
}
