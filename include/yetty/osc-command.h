#pragma once

#include <yetty/result.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <optional>

namespace yetty {

// Vendor ID for yetty OSC sequences
constexpr int YETTY_OSC_VENDOR_ID = 999999;

//-----------------------------------------------------------------------------
// OscCommand - parsed OSC command
//-----------------------------------------------------------------------------
enum class OscCommandType {
    Create,     // create -x 0 -y 0 -w 100 -h 100 -p image
    List,       // ls [--all]
    Plugins,    // plugins
    Kill,       // kill --id <id> | --plugin <name>
    Stop,       // stop --id <id> | --plugin <name>
    Start,      // start --id <id> | --plugin <name>
    Update,     // update --id <id>
    Unknown
};

struct CreateArgs {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;      // 0 = stretch to edge
    int32_t height = 0;     // 0 = stretch to edge
    std::string plugin;
    bool relative = false;  // Position relative to cursor
};

struct ListArgs {
    bool all = false;       // Include stopped layers
};

struct TargetArgs {
    std::string id;         // Target specific layer by ID
    std::string plugin;     // Target all layers of a plugin type
};

struct OscCommand {
    OscCommandType type = OscCommandType::Unknown;

    // Command-specific args (use appropriate one based on type)
    CreateArgs create;
    ListArgs list;
    TargetArgs target;      // For kill, stop, start, update

    // Plugin-specific args (raw string, passed to plugin)
    std::string pluginArgs;

    // Payload (base94 decoded)
    std::string payload;

    // Error message if parsing failed
    std::string error;

    bool isValid() const { return type != OscCommandType::Unknown && error.empty(); }
};

//-----------------------------------------------------------------------------
// OscCommandParser - parses OSC sequences into commands
//-----------------------------------------------------------------------------
class OscCommandParser {
public:
    OscCommandParser();
    ~OscCommandParser() = default;

    // Parse an OSC sequence (without the OSC prefix and ST suffix)
    // Format: <vendor-id>;<generic-args>;<plugin-args>;<payload>
    Result<OscCommand> parse(const std::string& sequence);

    // Generate a unique 8-character hash ID
    std::string generateId();

    // Base94 encoding/decoding
    static std::string base94Encode(const std::string& data);
    static std::string base94Decode(const std::string& encoded);

private:
    // Tokenize a string respecting quoted strings
    // "hello world" --foo bar -> ["hello world", "--foo", "bar"]
    std::vector<std::string> tokenize(const std::string& str);

    // Parse generic args into command
    Result<OscCommand> parseGenericArgs(const std::vector<std::string>& tokens);

    // Parse create command args
    Result<CreateArgs> parseCreateArgs(const std::vector<std::string>& tokens);

    // Parse list command args
    Result<ListArgs> parseListArgs(const std::vector<std::string>& tokens);

    // Parse target args (for kill, stop, start, update)
    Result<TargetArgs> parseTargetArgs(const std::vector<std::string>& tokens);

    // Split sequence by semicolon into fields
    std::vector<std::string> splitFields(const std::string& sequence);

    // ID generation
    std::mt19937 rng_;
    static constexpr const char* ID_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789";
    static constexpr size_t ID_LENGTH = 8;
};

//-----------------------------------------------------------------------------
// OscResponse - helper for building OSC responses
//-----------------------------------------------------------------------------
class OscResponse {
public:
    // Build success response
    static std::string success(const std::string& message = "");

    // Build error response
    static std::string error(const std::string& message);

    // Build list response (layers)
    static std::string layerList(const std::vector<std::tuple<std::string, std::string, int, int, int, int, bool>>& layers);

    // Build plugins list response
    static std::string pluginList(const std::vector<std::string>& plugins);
};

} // namespace yetty
