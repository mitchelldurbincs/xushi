#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

struct JsonValue {
    enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };
    Type type = NUL;

    bool bool_val = false;
    double number_val = 0.0;
    std::string string_val;
    std::vector<JsonValue> array_val;
    std::map<std::string, JsonValue> object_val;

    // Accessors — throw on type mismatch
    double as_number() const;
    int as_int() const;
    const std::string& as_string() const;
    bool as_bool() const;
    const std::vector<JsonValue>& as_array() const;
    const std::map<std::string, JsonValue>& as_object() const;

    // Object key lookup — throws if not an object or key missing
    const JsonValue& operator[](const std::string& key) const;

    // Check if object has key
    bool has(const std::string& key) const;

    // Get with default for optional fields
    double number_or(const std::string& key, double def) const;
    int int_or(const std::string& key, int def) const;
    const std::string& string_or(const std::string& key, const std::string& def) const;
};

// Parse a JSON string. Throws std::runtime_error on parse failure.
JsonValue json_parse(const std::string& input);

// Serialize a JsonValue to a compact single-line JSON string.
std::string json_serialize(const JsonValue& v);

// Builder helpers for convenient construction.
JsonValue json_null();
JsonValue json_bool(bool b);
JsonValue json_number(double n);
JsonValue json_string(const std::string& s);
JsonValue json_array(std::vector<JsonValue> elems);
JsonValue json_object(std::map<std::string, JsonValue> pairs);
