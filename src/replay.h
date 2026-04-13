#pragma once

#include "json.h"
#include <string>
#include <vector>
#include <fstream>

constexpr int kReplayFormatVersion = 2;

// Type-agnostic replay writer. Writes NDJSON (one JSON object per line).
struct ReplayWriter {
    std::ofstream file;

    explicit ReplayWriter(const std::string& path);
    void log(const JsonValue& event);
    void close();
    ~ReplayWriter();
};

// Type-agnostic replay reader. Reads NDJSON line by line.
struct ReplayReader {
    std::ifstream file;

    explicit ReplayReader(const std::string& path);

    // Read next event. Returns false at EOF.
    bool next(JsonValue& out);

    // Read all remaining events.
    std::vector<JsonValue> read_all();

    // Read all events and return only those matching the given type.
    std::vector<JsonValue> filter(const std::string& type);
};
