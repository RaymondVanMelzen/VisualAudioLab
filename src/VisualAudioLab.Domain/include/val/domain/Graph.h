#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace val::domain
{
/** Stable document-local identity; zero denotes no object and IDs are never recycled. */
using GraphId = std::uint64_t;

/** Finite patch-space coordinates, independent of pixels, zoom and JUCE. */
struct Position final
{
    double x{}, y{};
    bool operator==(const Position&) const = default;
};

/** Fixed structural ports on an editor node, not an audio/DSP capability declaration. */
enum class Port { input, output };

/** A node-local port reference; connection commands never retain view pointers. */
struct Endpoint final
{
    GraphId node{};
    Port port{Port::input};
    bool operator==(const Endpoint&) const = default;
};

/** Editable structural node with one named input and output and no processing state. */
struct Node final
{
    GraphId id{};
    std::string name;
    Position position;
    bool operator==(const Node&) const = default;
};

/** One directed output-to-input wire; input fan-in is forbidden, output fan-out allowed. */
struct Connection final
{
    GraphId id{};
    Endpoint source, destination;
    bool operator==(const Connection&) const = default;
};

/** Accepted transient graph owned by the workspace; views borrow immutable snapshots by ID. */
struct Graph final
{
    std::map<GraphId, Node> nodes;
    std::map<GraphId, Connection> connections;
    bool operator==(const Graph&) const = default;
};
}
