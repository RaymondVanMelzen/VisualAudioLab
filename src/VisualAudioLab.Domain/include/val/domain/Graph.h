#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace val::domain
{
/** Portable scalar parameter data, without DSP behavior or processor metadata. */
using Parameter = std::variant<std::monostate, bool, double, std::string>;
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
enum class EndpointRole { hardwareInput, hardwareOutput };
enum class AudioAdapter { none, monoToStereo };

/** A node-local port reference; connection commands never retain view pointers. */
struct Endpoint final
{
    GraphId node{};
    Port port{Port::input};
    /** Port identity is independent from direction; current built-ins expose port zero. */
    std::uint16_t index{};
    bool operator==(const Endpoint&) const = default;
};

/** Editable structural node with one named input and output and no processing state. */
struct Node final
{
    GraphId id{};
    std::string name;
    Position position;
    std::map<std::string, Parameter> parameters;
    bool operator==(const Node&) const = default;
};

/**
 * Fixed project-owned hardware lane row. Channel names are portable requests; driver indices
 * are resolved against the machine-local device inventory and never stored here.
 */
struct HardwareEndpoint final
{
    GraphId id{};
    EndpointRole role{EndpointRole::hardwareInput};
    std::string name;
    bool enabled{true};
    int layoutChannels{2};
    std::vector<std::string> channelNames;
    bool operator==(const HardwareEndpoint&) const = default;
};

/** One directed, typed-port wire; only explicit Sum inputs accept fan-in. */
struct Connection final
{
    GraphId id{};
    Endpoint source, destination;
    /** Visible/persisted conversion; no implicit channel truncation or summing. */
    AudioAdapter adapter{AudioAdapter::none};
    bool operator==(const Connection&) const = default;
};

/** Accepted structural graph owned by its project patch; views resolve immutable content by ID. */
struct Graph final
{
    std::map<GraphId, Node> nodes;
    std::map<GraphId, HardwareEndpoint> endpoints;
    std::map<GraphId, Connection> connections;
    bool operator==(const Graph&) const = default;
};
}
