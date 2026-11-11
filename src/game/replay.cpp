#include "bo2/game/replay.hpp"

#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace bo2::game {

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r");
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char character : line) {
        if (character == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    fields.push_back(trim(field));
    return fields;
}

std::size_t require_column(const std::map<std::string, std::size_t>& columns,
                           const std::string& name, std::size_t row) {
    const auto found = columns.find(name);
    if (found == columns.end()) {
        throw std::runtime_error("replay CSV is missing column '" + name + "'");
    }
    (void)row;
    return found->second;
}

const std::string& field(const std::vector<std::string>& fields, std::size_t index,
                         const std::string& name, std::size_t line) {
    if (index >= fields.size()) {
        throw std::runtime_error("replay CSV line " + std::to_string(line) +
                                 " has no '" + name + "' field");
    }
    return fields[index];
}

double parse_double(const std::string& text, const std::string& name, std::size_t line) {
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("replay CSV line " + std::to_string(line) +
                                 " has an invalid " + name + ": " + text);
    }
    if (consumed != text.size()) {
        throw std::runtime_error("replay CSV line " + std::to_string(line) +
                                 " has an invalid " + name + ": " + text);
    }
    return value;
}

std::uint64_t parse_u64(const std::string& text, const std::string& name, std::size_t line) {
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("replay CSV line " + std::to_string(line) +
                                 " has an invalid " + name + ": " + text);
    }
    if (consumed != text.size()) {
        throw std::runtime_error("replay CSV line " + std::to_string(line) +
                                 " has an invalid " + name + ": " + text);
    }
    return static_cast<std::uint64_t>(value);
}

bool parse_bool(const std::string& text, const std::string& name, std::size_t line) {
    std::string lower = text;
    for (char& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (lower == "1" || lower == "true") {
        return true;
    }
    if (lower == "0" || lower == "false" || lower.empty()) {
        return false;
    }
    throw std::runtime_error("replay CSV line " + std::to_string(line) +
                             " has an invalid " + name + ": " + text);
}

}  // namespace

ReplayRoute parse_replay_csv(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    std::size_t line_number = 0;
    std::vector<std::string> header;
    std::map<std::string, std::size_t> columns;
    ReplayRoute route;
    bool first_tick = true;
    while (std::getline(stream, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields = split_csv(line);
        if (header.empty()) {
            header = fields;
            for (std::size_t index = 0; index < header.size(); ++index) {
                columns.emplace(header[index], index);
            }
            require_column(columns, "frame", line_number);
            require_column(columns, "dt", line_number);
            require_column(columns, "phase", line_number);
            require_column(columns, "move_x", line_number);
            require_column(columns, "move_y", line_number);
            require_column(columns, "run", line_number);
            require_column(columns, "jump", line_number);
            require_column(columns, "attack", line_number);
            require_column(columns, "start_x", line_number);
            require_column(columns, "start_y", line_number);
            require_column(columns, "start_z", line_number);
            require_column(columns, "start_yaw_deg", line_number);
            continue;
        }
        ReplayTick tick;
        tick.frame = parse_u64(field(fields, columns.at("frame"), "frame", line_number), "frame",
                               line_number);
        tick.seconds =
            parse_double(field(fields, columns.at("dt"), "dt", line_number), "dt", line_number);
        tick.phase = field(fields, columns.at("phase"), "phase", line_number);
        tick.move_x = static_cast<float>(parse_double(
            field(fields, columns.at("move_x"), "move_x", line_number), "move_x", line_number));
        tick.move_y = static_cast<float>(parse_double(
            field(fields, columns.at("move_y"), "move_y", line_number), "move_y", line_number));
        tick.run = parse_bool(field(fields, columns.at("run"), "run", line_number), "run",
                              line_number);
        tick.jump = parse_bool(field(fields, columns.at("jump"), "jump", line_number), "jump",
                               line_number);
        tick.attack = parse_bool(field(fields, columns.at("attack"), "attack", line_number),
                                 "attack", line_number);
        if (first_tick) {
            route.start.x = static_cast<float>(parse_double(
                field(fields, columns.at("start_x"), "start_x", line_number), "start_x",
                line_number));
            route.start.y = static_cast<float>(parse_double(
                field(fields, columns.at("start_y"), "start_y", line_number), "start_y",
                line_number));
            route.start.z = static_cast<float>(parse_double(
                field(fields, columns.at("start_z"), "start_z", line_number), "start_z",
                line_number));
            route.yaw_degrees = static_cast<float>(parse_double(
                field(fields, columns.at("start_yaw_deg"), "start_yaw_deg", line_number),
                "start_yaw_deg", line_number));
            first_tick = false;
        }
        route.ticks.push_back(std::move(tick));
    }
    if (header.empty()) {
        throw std::runtime_error("replay CSV is empty");
    }
    if (route.ticks.empty()) {
        throw std::runtime_error("replay CSV has no ticks");
    }
    return route;
}

}  // namespace bo2::game
