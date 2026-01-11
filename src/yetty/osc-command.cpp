#include <yetty/osc-command.h>
#include <ytrace/ytrace.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace yetty {

//-----------------------------------------------------------------------------
// OscCommandParser
//-----------------------------------------------------------------------------

OscCommandParser::OscCommandParser() {
    // Seed RNG with high-resolution clock
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng_.seed(static_cast<uint32_t>(seed));
}

std::string OscCommandParser::generateId() {
    std::string id;
    id.reserve(ID_LENGTH);

    std::uniform_int_distribution<size_t> dist(0, 35);  // 26 letters + 10 digits
    for (size_t i = 0; i < ID_LENGTH; ++i) {
        id += ID_CHARS[dist(rng_)];
    }

    return id;
}

std::vector<std::string> OscCommandParser::splitFields(const std::string& sequence) {
    // Split into exactly 4 fields: vendor-id;generic-args;plugin-args;payload
    // Only split on first 3 semicolons - payload may contain semicolons
    std::vector<std::string> fields;
    size_t start = 0;

    for (int i = 0; i < 3; ++i) {
        size_t pos = sequence.find(';', start);
        if (pos == std::string::npos) {
            fields.push_back(sequence.substr(start));
            return fields;
        }
        fields.push_back(sequence.substr(start, pos - start));
        start = pos + 1;
    }

    // Remaining is payload (may contain semicolons)
    fields.push_back(sequence.substr(start));
    return fields;
}

std::vector<std::string> OscCommandParser::tokenize(const std::string& str) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

Result<OscCommand> OscCommandParser::parse(const std::string& sequence) {
    OscCommand cmd;

    // Split into fields: vendor-id;generic-args;plugin-args;payload
    auto fields = splitFields(sequence);

    if (fields.size() < 2) {
        cmd.error = "invalid sequence: expected at least vendor-id and command";
        return Ok(cmd);
    }

    // Check vendor ID
    int vendorId = 0;
    try {
        vendorId = std::stoi(fields[0]);
    } catch (...) {
        cmd.error = "invalid vendor ID";
        return Ok(cmd);
    }

    if (vendorId != YETTY_OSC_VENDOR_ID) {
        cmd.error = "unknown vendor ID: " + std::to_string(vendorId);
        return Ok(cmd);
    }

    // Parse generic args (field 1)
    auto tokens = tokenize(fields[1]);
    if (tokens.empty()) {
        cmd.error = "empty command";
        return Ok(cmd);
    }

    auto parseResult = parseGenericArgs(tokens);
    if (!parseResult) {
        cmd.error = error_msg(parseResult);
        return Ok(cmd);
    }
    cmd = *parseResult;

    // Plugin args (field 2, optional)
    if (fields.size() > 2) {
        cmd.pluginArgs = fields[2];
    }

    // Payload (field 3, optional, base94 encoded)
    if (fields.size() > 3 && !fields[3].empty()) {
        cmd.payload = base94Decode(fields[3]);
    }

    return Ok(cmd);
}

Result<OscCommand> OscCommandParser::parseGenericArgs(const std::vector<std::string>& tokens) {
    OscCommand cmd;

    if (tokens.empty()) {
        return Err<OscCommand>("empty command");
    }

    const std::string& command = tokens[0];

    if (command == "create") {
        cmd.type = OscCommandType::Create;
        auto result = parseCreateArgs(tokens);
        if (!result) {
            return Err<OscCommand>("create: " + error_msg(result));
        }
        cmd.create = *result;
    }
    else if (command == "ls" || command == "list") {
        cmd.type = OscCommandType::List;
        auto result = parseListArgs(tokens);
        if (!result) {
            return Err<OscCommand>("ls: " + error_msg(result));
        }
        cmd.list = *result;
    }
    else if (command == "plugins") {
        cmd.type = OscCommandType::Plugins;
    }
    else if (command == "kill") {
        cmd.type = OscCommandType::Kill;
        auto result = parseTargetArgs(tokens);
        if (!result) {
            return Err<OscCommand>("kill: " + error_msg(result));
        }
        cmd.target = *result;
    }
    else if (command == "stop") {
        cmd.type = OscCommandType::Stop;
        auto result = parseTargetArgs(tokens);
        if (!result) {
            return Err<OscCommand>("stop: " + error_msg(result));
        }
        cmd.target = *result;
    }
    else if (command == "start") {
        cmd.type = OscCommandType::Start;
        auto result = parseTargetArgs(tokens);
        if (!result) {
            return Err<OscCommand>("start: " + error_msg(result));
        }
        cmd.target = *result;
    }
    else if (command == "update") {
        cmd.type = OscCommandType::Update;
        auto result = parseTargetArgs(tokens);
        if (!result) {
            return Err<OscCommand>("update: " + error_msg(result));
        }
        cmd.target = *result;
    }
    else {
        return Err<OscCommand>("unknown command: " + command);
    }

    return Ok(cmd);
}

Result<CreateArgs> OscCommandParser::parseCreateArgs(const std::vector<std::string>& tokens) {
    CreateArgs args;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        if (token == "-x" || token == "--x-pos") {
            if (++i >= tokens.size()) return Err<CreateArgs>("missing value for " + token);
            args.x = std::stoi(tokens[i]);
        }
        else if (token == "-y" || token == "--y-pos") {
            if (++i >= tokens.size()) return Err<CreateArgs>("missing value for " + token);
            args.y = std::stoi(tokens[i]);
        }
        else if (token == "-w" || token == "--width") {
            if (++i >= tokens.size()) return Err<CreateArgs>("missing value for " + token);
            args.width = std::stoi(tokens[i]);
        }
        else if (token == "-h" || token == "--height") {
            if (++i >= tokens.size()) return Err<CreateArgs>("missing value for " + token);
            args.height = std::stoi(tokens[i]);
        }
        else if (token == "-p" || token == "--plugin") {
            if (++i >= tokens.size()) return Err<CreateArgs>("missing value for " + token);
            args.plugin = tokens[i];
        }
        else if (token == "-r" || token == "--relative") {
            args.relative = true;
        }
        else {
            return Err<CreateArgs>("unknown option: " + token);
        }
    }

    if (args.plugin.empty()) {
        return Err<CreateArgs>("--plugin/-p is required");
    }

    return Ok(args);
}

Result<ListArgs> OscCommandParser::parseListArgs(const std::vector<std::string>& tokens) {
    ListArgs args;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        if (token == "-a" || token == "--all") {
            args.all = true;
        }
        else {
            return Err<ListArgs>("unknown option: " + token);
        }
    }

    return Ok(args);
}

Result<TargetArgs> OscCommandParser::parseTargetArgs(const std::vector<std::string>& tokens) {
    TargetArgs args;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        if (token == "--id") {
            if (++i >= tokens.size()) return Err<TargetArgs>("missing value for --id");
            args.id = tokens[i];
        }
        else if (token == "--plugin" || token == "-p") {
            if (++i >= tokens.size()) return Err<TargetArgs>("missing value for --plugin");
            args.plugin = tokens[i];
        }
        else {
            return Err<TargetArgs>("unknown option: " + token);
        }
    }

    if (args.id.empty() && args.plugin.empty()) {
        return Err<TargetArgs>("--id or --plugin is required");
    }

    return Ok(args);
}

//-----------------------------------------------------------------------------
// Base94 encoding/decoding
//-----------------------------------------------------------------------------

std::string OscCommandParser::base94Encode(const std::string& data) {
    std::string result;
    result.reserve(data.size() * 2);

    for (unsigned char byte : data) {
        int value = byte;
        char c1 = '!' + (value / 94);
        char c2 = '!' + (value % 94);
        result.push_back(c1);
        result.push_back(c2);
    }

    return result;
}

std::string OscCommandParser::base94Decode(const std::string& encoded) {
    if (encoded.empty()) return "";

    const size_t len = encoded.size();
    std::string result;
    result.resize(len / 2);

    const char* src = encoded.data();
    char* dst = &result[0];
    size_t outIdx = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned char c1 = src[i] - '!';
        unsigned char c2 = src[i + 1] - '!';
        dst[outIdx++] = static_cast<char>(c1 * 94 + c2);
    }

    result.resize(outIdx);
    return result;
}

//-----------------------------------------------------------------------------
// OscResponse
//-----------------------------------------------------------------------------

std::string OscResponse::success(const std::string& message) {
    if (message.empty()) {
        return "";  // Silent success
    }
    return message + "\n";
}

std::string OscResponse::error(const std::string& message) {
    return "error: " + message + "\n";
}

std::string OscResponse::widgetList(const std::vector<std::tuple<std::string, std::string, int, int, int, int, bool>>& widgets) {
    if (widgets.empty()) {
        return "no widgets\n";
    }

    std::string result;
    result += "ID        PLUGIN          X     Y     W     H  STATE\n";
    result += "--------  --------------  ----  ----  ----  ----  -------\n";

    for (const auto& [id, plugin, x, y, w, h, running] : widgets) {
        char line[128];
        snprintf(line, sizeof(line), "%-8s  %-14s  %4d  %4d  %4d  %4d  %s\n",
                 id.c_str(), plugin.c_str(), x, y, w, h,
                 running ? "running" : "stopped");
        result += line;
    }

    return result;
}

std::string OscResponse::pluginList(const std::vector<std::string>& plugins) {
    if (plugins.empty()) {
        return "no plugins available\n";
    }

    std::string result = "available plugins:\n";
    for (const auto& name : plugins) {
        result += "  " + name + "\n";
    }
    return result;
}

} // namespace yetty
