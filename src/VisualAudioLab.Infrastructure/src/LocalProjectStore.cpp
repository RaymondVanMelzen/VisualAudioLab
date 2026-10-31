#include <val/infrastructure/LocalProjectStore.h>
#include <juce_core/juce_core.h>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>

namespace val::infrastructure
{
namespace
{
using namespace application;
using juce::var;
constexpr std::int64_t maximumBytes = 64 * 1024 * 1024;
constexpr std::array<const char*, 5> panelNames{"canvas", "modules", "explorer", "properties", "audio"};

/** Internal parse diagnostic; only schema errors are caught at the codec boundary. */
class SchemaError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition)
        throw SchemaError(message);
}

/**
 * Guards JUCE's permissive DOM conversion against ambiguous/lossy input before parsing.
 * The schema supports finite binary64 decimals and signed-64-bit integer JSON literals;
 * stable object IDs use strings. Excess precision is rejected, never rounded invisibly.
 */
class JsonGuard final
{
public:
    explicit JsonGuard(const std::string& source) : source_(source) {}
    void run()
    {
        value(0);
        space();
        check(position_ == source_.size(), "Unexpected content after the project JSON.");
    }
private:
    void space()
    {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])))
            ++position_;
    }
    std::string string()
    {
        const auto start = position_++;
        while (position_ < source_.size())
        {
            const auto ch = source_[position_++];
            if (ch == '"') break;
            if (ch == '\\' && position_ < source_.size())
            {
                check(source_.compare(position_, 5, "u0000") != 0, "NUL strings are unsupported; original preserved.");
                ++position_;
            }
        }
        const auto token = source_.substr(start, position_ - start);
        return juce::JSON::fromString(juce::String::fromUTF8(token.c_str())).toString().toStdString();
    }
    void value(int depth)
    {
        check(depth <= 64, "Project JSON nesting exceeds the supported depth of 64.");
        space();
        if (position_ >= source_.size()) return;
        const auto ch = source_[position_];
        if (ch == '"') { string(); return; }
        if (ch == '{' || ch == '[')
        {
            ++position_;
            const auto end = ch == '{' ? '}' : ']';
            std::set<std::string> keys;
            space();
            while (position_ < source_.size() && source_[position_] != end)
            {
                if (ch == '{')
                {
                    space();
                    check(position_ < source_.size() && source_[position_] == '"', "Invalid JSON object key.");
                    check(keys.insert(string()).second, "Duplicate JSON object keys are ambiguous; original preserved.");
                    space();
                    check(position_ < source_.size() && source_[position_++] == ':', "Invalid JSON object.");
                }
                value(depth + 1);
                space();
                if (position_ < source_.size() && source_[position_] == ',')
                {
                    ++position_; space();
                    check(position_ < source_.size() && source_[position_] != end, "Trailing JSON commas are invalid.");
                }
                else break;
            }
            check(position_ < source_.size() && source_[position_++] == end, "Invalid JSON container.");
            return;
        }
        const auto start = position_;
        while (position_ < source_.size() && source_[position_] != ',' && source_[position_] != '}'
               && source_[position_] != ']' && !std::isspace(static_cast<unsigned char>(source_[position_])))
            ++position_;
        const auto token = source_.substr(start, position_ - start);
        if (token.empty() || (token[0] != '-' && (token[0] < '0' || token[0] > '9'))) return;
        if (token.find_first_of(".eE") == std::string::npos)
        {
            std::int64_t integerValue{};
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), integerValue);
            check(parsed.ec == std::errc{}, "Integer literal exceeds signed 64-bit precision; use a string extension field.");
        }
        else
        {
            auto digits = token.substr(0, token.find_first_of("eE"));
            digits.erase(std::remove_if(digits.begin(), digits.end(), [](char c) { return c == '.' || c == '-'; }), digits.end());
            const auto first = digits.find_first_not_of('0'), last = digits.find_last_not_of('0');
            check(first == std::string::npos || last - first + 1 <= 17,
                  "Decimal exceeds supported 17-digit precision; original preserved.");
            const auto parsed = juce::JSON::fromString(juce::String(token));
            check(std::isfinite(static_cast<double>(parsed)), "Non-finite JSON number is unsupported.");
            check(first == std::string::npos || static_cast<double>(parsed) != 0,
                  "Decimal underflows binary64 precision; original preserved.");
        }
    }
    const std::string& source_;
    std::size_t position_{};
};

var object() { return var(new juce::DynamicObject()); }
const var& field(const var& value, const char* name)
{
    check(value.getDynamicObject() != nullptr && value.getDynamicObject()->hasProperty(name), "A required project field is missing.");
    return value.getDynamicObject()->getProperty(name);
}
void put(var& value, const char* name, var item) { value.getDynamicObject()->setProperty(name, std::move(item)); }
var nested(var& parent, const char* name)
{
    auto value = parent.getProperty(name, {});
    if (value.getDynamicObject() == nullptr)
        value = object();
    put(parent, name, value);
    return value;
}
std::string text(const var& value)
{
    check(value.isString(), "Expected a JSON string.");
    return value.toString().toStdString();
}
double number(const var& value)
{
    check(value.isInt() || value.isInt64() || value.isDouble(), "Expected a finite JSON number.");
    if (value.isInt64())
    {
        const auto integral = static_cast<juce::int64>(value);
        check(integral >= -9007199254740991LL && integral <= 9007199254740991LL,
              "Known numeric fields require exact binary64-compatible integers; original preserved.");
    }
    const auto result = static_cast<double>(value);
    check(std::isfinite(result), "Project numbers must be finite.");
    return result;
}
int integer(const var& value)
{
    check(value.isInt() || value.isInt64(), "Expected a JSON integer.");
    const auto result = static_cast<juce::int64>(value);
    check(result >= std::numeric_limits<int>::min() && result <= std::numeric_limits<int>::max(), "Integer is out of range.");
    return static_cast<int>(result);
}
bool boolean(const var& value)
{
    check(value.isBool(), "Expected a JSON boolean.");
    return static_cast<bool>(value);
}
const juce::Array<var>& array(const var& value)
{
    check(value.isArray(), "Expected a JSON array.");
    return *value.getArray();
}
domain::GraphId identity(const var& value)
{
    const auto token = text(value);
    domain::GraphId result{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
    check(!token.empty() && token[0] != '0' && parsed.ec == std::errc{}
          && parsed.ptr == token.data() + token.size() && result != std::numeric_limits<domain::GraphId>::max(),
          "Object IDs must be positive canonical decimal strings below UINT64_MAX.");
    return result;
}
var idValue(domain::GraphId id) { return juce::String(id); }
var indexedObject(const var& items, domain::GraphId id)
{
    if (items.isArray())
        for (const auto& item : *items.getArray())
            if (item.getDynamicObject() != nullptr && item.getProperty("id", {}).toString() == juce::String(id))
                return item;
    return object();
}
domain::Endpoint endpoint(const var& value)
{
    const auto port = text(field(value, "port"));
    check(port == "in" || port == "out", "Unknown port identity; this reader cannot safely edit the project.");
    return {identity(field(value, "node")), port == "in" ? domain::Port::input : domain::Port::output};
}
void writeEndpoint(var& parent, const char* name, domain::Endpoint endpointValue)
{
    auto value = nested(parent, name);
    put(value, "node", idValue(endpointValue.node));
    put(value, "port", endpointValue.port == domain::Port::input ? "in" : "out");
}
juce::File juceFile(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error || path.empty())
        throw SchemaError("Cannot resolve the selected file location.");
    const auto utf8 = absolute.u8string();
    return juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size())));
}
std::string readText(const juce::File& file, std::int64_t limit = maximumBytes)
{
    check(file.existsAsFile() && file.getSize() <= limit, "File is missing, not regular, or exceeds the supported size limit.");
    juce::FileInputStream input(file);
    check(input.openedOk(), "Cannot open the selected file. Check permissions and availability.");
    const auto content = input.readEntireStreamAsString();
    check(input.getStatus().wasOk(), "Cannot read the complete file; the active project is unchanged.");
    return content.toStdString();
}
Result atomicWrite(const juce::File& target, const std::string& content,
                   const std::optional<std::string>& expected = std::nullopt, bool requireMissing = false)
{
    if (!target.getParentDirectory().isDirectory())
        return Result::failure("Destination directory does not exist. Choose an existing writable directory.");
    const juce::TemporaryFile temporary(target);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (!stream.openedOk() || !stream.write(content.data(), content.size()))
            return Result::failure("Cannot write the complete temporary file. Check permissions and free space; original preserved.");
        stream.flush();
        if (stream.getStatus().failed())
            return Result::failure("Cannot flush the temporary file. Original preserved.");
    }
    if (expected && (!target.existsAsFile() || readText(target) != *expected))
        return Result::failure("Destination changed while saving. Original preserved; use a different Save As location.");
    if (requireMissing && target.exists())
        return Result::failure("A file appeared at the destination while saving. Choose a different Save As location.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return Result::failure("Cannot atomically replace the destination. Close other users of the file; original preserved.");
    return Result::success();
}
}

application::ProjectReadResult LocalProjectStore::decode(const std::string& source)
{
    try
    {
        check(source.size() <= maximumBytes, "Project exceeds the 64 MiB supported size limit.");
        JsonGuard(source).run();
        var root;
        check(juce::JSON::parse(juce::String::fromUTF8(source.data(), static_cast<int>(source.size())), root).wasOk()
              && root.getDynamicObject() != nullptr, "Project must contain a JSON object.");
        check(text(field(root, "format")) == "VisualAudioLab", "Not a Visual Audio Lab project.");
        const auto& version = field(root, "version");
        check(integer(field(version, "major")) == 1 && integer(field(version, "minor")) >= 0,
              "Unsupported breaking project version. Use a compatible application; the original is preserved.");
        const auto& reader = field(root, "minimumReader");
        check(integer(field(reader, "major")) == 1 && integer(field(reader, "minor")) == 0,
              "Project requires a newer reader. It will not be opened or overwritten.");
        check(array(field(root, "requiredFeatures")).isEmpty(),
              "Project requires unsupported features. It will not be opened or overwritten.");
        ProjectState state;
        state.workspace.name = text(field(root, "name"));
        state.activePatch = identity(field(root, "activePatch"));
        state.patches.clear();
        for (const auto& item : array(field(root, "patches")))
        {
            Patch patch;
            patch.id = identity(field(item, "id"));
            patch.name = text(field(item, "name"));
            const auto& viewport = field(item, "viewport");
            patch.viewport = {number(field(viewport, "zoom")), number(field(viewport, "x")), number(field(viewport, "y"))};
            const auto& graph = field(item, "graph");
            for (const auto& nodeValue : array(field(graph, "nodes")))
            {
                domain::Node node;
                node.id = identity(field(nodeValue, "id"));
                node.name = text(field(nodeValue, "name"));
                const auto& position = field(nodeValue, "position");
                node.position = {number(field(position, "x")), number(field(position, "y"))};
                const auto& parameters = field(nodeValue, "parameters");
                check(parameters.getDynamicObject() != nullptr, "Parameters must be a scalar-valued JSON object.");
                for (const auto& entry : parameters.getDynamicObject()->getProperties())
                {
                    domain::Parameter value;
                    if (entry.value.isBool()) value = static_cast<bool>(entry.value);
                    else if (entry.value.isString()) value = text(entry.value);
                    else if (entry.value.isVoid()) value = std::monostate{};
                    else value = number(entry.value);
                    node.parameters.emplace(entry.name.toString().toStdString(), std::move(value));
                }
                check(patch.graph.nodes.emplace(node.id, std::move(node)).second, "Duplicate node ID.");
            }
            for (const auto& wireValue : array(field(graph, "connections")))
            {
                domain::Connection wire{identity(field(wireValue, "id")), endpoint(field(wireValue, "source")),
                                        endpoint(field(wireValue, "destination"))};
                check(patch.graph.connections.emplace(wire.id, wire).second, "Duplicate connection ID.");
            }
            check(state.patches.emplace(patch.id, std::move(patch)).second, "Duplicate patch ID.");
        }
        const auto& layout = field(root, "layout");
        const auto& window = field(layout, "window");
        state.layout.x = integer(field(window, "x"));
        state.layout.y = integer(field(window, "y"));
        state.layout.width = integer(field(window, "width"));
        state.layout.height = integer(field(window, "height"));
        std::array<bool, 5> seen{};
        for (const auto& panel : array(field(layout, "panels")))
        {
            const auto name = text(field(panel, "id"));
            const auto found = std::find(panelNames.begin(), panelNames.end(), name);
            if (found == panelNames.end())
                continue; // Future panel records are retained verbatim in source.
            const auto index = static_cast<std::size_t>(found - panelNames.begin());
            check(!seen[index], "Duplicate panel identity.");
            seen[index] = true;
            state.layout.docks[index] = integer(field(panel, "dock"));
            state.layout.visible[index] = boolean(field(panel, "visible"));
        }
        check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "Required shell panels are missing.");
        const auto& ratios = array(field(layout, "ratios"));
        const auto& selected = array(field(layout, "selectedPanels"));
        check(ratios.size() == 3 && selected.size() == 4, "Invalid dock ratio/tab arrays.");
        for (int i = 0; i < 3; ++i) state.layout.ratios[i] = number(ratios[i]);
        for (int i = 0; i < 4; ++i) state.layout.selectedPanels[i] = integer(selected[i]);
        const auto validation = validateProject(state);
        check(validation.succeeded, validation.error.c_str());
        return {ProjectFile{std::move(state), source}, {}};
    }
    catch (const SchemaError& error) { return {{}, error.what()}; }
}

application::Result LocalProjectStore::encode(application::ProjectFile& file)
{
    if (const auto validation = application::validateProject(file.state); !validation.succeeded)
        return validation;
    var root;
    if (!file.source.empty())
    {
        const auto original = decode(file.source);
        if (!original.file)
            return Result::failure("Cannot preserve the source document: " + original.error);
        juce::JSON::parse(juce::String::fromUTF8(file.source.c_str()), root);
    }
    else
    {
        root = object();
        auto version = nested(root, "version");
        put(version, "major", 1); put(version, "minor", 0);
        auto reader = nested(root, "minimumReader");
        put(reader, "major", 1); put(reader, "minor", 0);
        put(root, "requiredFeatures", juce::Array<var>{});
    }
    put(root, "format", "VisualAudioLab");
    put(root, "name", juce::String::fromUTF8(file.state.workspace.name.c_str()));
    put(root, "activePatch", idValue(file.state.activePatch));
    juce::Array<var> patches;
    for (const auto& [id, patch] : file.state.patches)
    {
        auto item = indexedObject(root.getProperty("patches", {}), id);
        put(item, "id", idValue(id)); put(item, "name", juce::String::fromUTF8(patch.name.c_str()));
        auto viewport = nested(item, "viewport");
        put(viewport, "zoom", patch.viewport.zoom); put(viewport, "x", patch.viewport.x); put(viewport, "y", patch.viewport.y);
        auto graph = nested(item, "graph");
        juce::Array<var> nodes, wires;
        for (const auto& [nodeId, node] : patch.graph.nodes)
        {
            auto nodeValue = indexedObject(graph.getProperty("nodes", {}), nodeId);
            put(nodeValue, "id", idValue(nodeId)); put(nodeValue, "name", juce::String::fromUTF8(node.name.c_str()));
            auto position = nested(nodeValue, "position");
            put(position, "x", node.position.x); put(position, "y", node.position.y);
            auto parameters = object();
            for (const auto& [key, value] : node.parameters)
                std::visit([&](const auto& scalar)
                {
                    using T = std::decay_t<decltype(scalar)>;
                    var encoded;
                    if constexpr (std::is_same_v<T, std::string>) encoded = juce::String::fromUTF8(scalar.c_str());
                    else if constexpr (!std::is_same_v<T, std::monostate>) encoded = scalar;
                    parameters.getDynamicObject()->setProperty(juce::Identifier(juce::String::fromUTF8(key.c_str())), encoded);
                }, value);
            put(nodeValue, "parameters", parameters);
            nodes.add(nodeValue);
        }
        for (const auto& [wireId, wire] : patch.graph.connections)
        {
            auto wireValue = indexedObject(graph.getProperty("connections", {}), wireId);
            put(wireValue, "id", idValue(wireId));
            writeEndpoint(wireValue, "source", wire.source);
            writeEndpoint(wireValue, "destination", wire.destination);
            wires.add(wireValue);
        }
        put(graph, "nodes", nodes); put(graph, "connections", wires);
        patches.add(item);
    }
    put(root, "patches", patches);
    auto layout = nested(root, "layout");
    auto window = nested(layout, "window");
    put(window, "x", file.state.layout.x); put(window, "y", file.state.layout.y);
    put(window, "width", file.state.layout.width); put(window, "height", file.state.layout.height);
    auto panels = layout.getProperty("panels", {});
    juce::Array<var> panelValues;
    for (std::size_t i = 0; i < panelNames.size(); ++i)
    {
        auto panel = object();
        if (panels.isArray())
            for (const auto& old : *panels.getArray())
                if (old.getProperty("id", {}).toString() == panelNames[i])
                    panel = old;
        put(panel, "id", panelNames[i]); put(panel, "dock", file.state.layout.docks[i]);
        put(panel, "visible", file.state.layout.visible[i]);
        panelValues.add(panel);
    }
    if (panels.isArray())
        for (const auto& old : *panels.getArray())
            if (std::find(panelNames.begin(), panelNames.end(), old.getProperty("id", {}).toString().toStdString()) == panelNames.end())
                panelValues.add(old);
    put(layout, "panels", panelValues);
    juce::Array<var> ratios, selected;
    for (auto ratio : file.state.layout.ratios) ratios.add(ratio);
    for (auto panel : file.state.layout.selectedPanels) selected.add(panel);
    put(layout, "ratios", ratios); put(layout, "selectedPanels", selected);
    auto encoded = juce::JSON::toString(root, false, 17).toStdString() + "\n";
    if (encoded.size() > maximumBytes)
        return Result::failure("Encoded project exceeds 64 MiB. Original preserved.");
    const auto verified = decode(encoded);
    if (!verified.file || verified.file->state != file.state)
        return Result::failure("Project cannot be represented losslessly by this schema. Original preserved.");
    file.source.swap(encoded);
    return Result::success();
}

application::ProjectReadResult LocalProjectStore::read(const std::filesystem::path& path)
{
    try
    {
        const auto file = juceFile(path);
        check(file.hasFileExtension(".valab"), "Select a project with the .valab extension.");
        return decode(readText(file));
    }
    catch (const SchemaError& error) { return {{}, error.what()}; }
}

application::Result LocalProjectStore::write(const std::filesystem::path& path, application::ProjectFile& file,
                                            const std::optional<std::string>& expectedSource)
{
    try
    {
        const auto target = juceFile(path);
        check(target.hasFileExtension(".valab"), "Project filenames must end with .valab.");
        std::optional<std::string> existingContent;
        if (target.exists())
        {
            const auto existing = readText(target);
            existingContent = existing;
            const auto parsed = decode(existing);
            check(parsed.file.has_value(), "Destination is unsupported or corrupt. Choose a different Save As path; original preserved.");
            check(!expectedSource || *expectedSource == existing,
                  "Project changed on disk. Load that version or Save As a different file to avoid losing external changes.");
        }
        else
            check(!expectedSource, "The active file was removed. Use Save As at a different location.");
        auto prepared = file;
        if (auto result = encode(prepared); !result.succeeded)
            return result;
        if (auto result = atomicWrite(target, prepared.source, existingContent, !existingContent.has_value()); !result.succeeded)
            return result;
        file.source.swap(prepared.source);
        return Result::success();
    }
    catch (const SchemaError& error) { return Result::failure(error.what()); }
}

application::Result LocalProjectStore::readRecent(std::vector<std::filesystem::path>& paths)
{
    try
    {
        const auto file = juceFile(recentFile_);
        if (!file.exists())
            return Result::success();
        var root;
        check(juce::JSON::parse(juce::String::fromUTF8(readText(file, 64 * 1024).c_str()), root).wasOk(),
              "Recent Files metadata is corrupt; it has been preserved.");
        check(integer(field(root, "version")) == 1, "Unsupported Recent Files metadata; original preserved.");
        std::vector<std::filesystem::path> result;
        for (const auto& value : array(field(root, "paths")))
        {
            const auto utf8 = text(value);
            result.emplace_back(std::u8string(utf8.begin(), utf8.end()));
        }
        paths.swap(result);
        return Result::success();
    }
    catch (const SchemaError& error) { return Result::failure(error.what()); }
}

application::Result LocalProjectStore::writeRecent(const std::vector<std::filesystem::path>& paths)
{
    if (paths.size() > 10)
        return Result::failure("Recent Files metadata is limited to ten paths.");
    std::vector<std::filesystem::path> original;
    if (auto result = readRecent(original); !result.succeeded)
        return result;
    try
    {
        auto root = object();
        put(root, "version", 1);
        juce::Array<var> items;
        for (const auto& path : paths)
        {
            const auto utf8 = path.u8string();
            items.add(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size())));
        }
        put(root, "paths", items);
        const auto content = juce::JSON::toString(root, false).toStdString() + "\n";
        if (content.size() > 64 * 1024)
            return Result::failure("Recent Files metadata exceeds 64 KiB; the existing list is preserved.");
        const auto target = juceFile(recentFile_);
        if (target.getParentDirectory().createDirectory().failed())
            return Result::failure("Cannot create the Recent Files metadata directory.");
        return atomicWrite(target, content);
    }
    catch (const SchemaError& error) { return Result::failure(error.what()); }
}
}
