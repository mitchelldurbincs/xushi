#include "replay.h"

ReplayWriter::ReplayWriter(const std::string& path)
    : file(path) {
    if (!file.is_open())
        throw std::runtime_error("cannot open replay file for writing: " + path);
}

void ReplayWriter::log(const JsonValue& event) {
    (void)kReplayFormatVersion; // replay header carries explicit format for viewer compatibility.
    file << json_serialize(event) << '\n';
}

void ReplayWriter::close() {
    if (file.is_open()) file.close();
}

ReplayWriter::~ReplayWriter() {
    close();
}

ReplayReader::ReplayReader(const std::string& path)
    : file(path) {
    if (!file.is_open())
        throw std::runtime_error("cannot open replay file for reading: " + path);
}

bool ReplayReader::next(JsonValue& out) {
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        out = json_parse(line);
        return true;
    }
    return false;
}

std::vector<JsonValue> ReplayReader::read_all() {
    std::vector<JsonValue> events;
    JsonValue v;
    while (next(v)) events.push_back(std::move(v));
    return events;
}

std::vector<JsonValue> ReplayReader::filter(const std::string& type) {
    std::vector<JsonValue> result;
    JsonValue v;
    while (next(v)) {
        if (v.has("type") && v["type"].as_string() == type)
            result.push_back(std::move(v));
    }
    return result;
}
