#include "json.h"
#include <cstdlib>
#include <cctype>

// --- JsonValue accessors ---

double JsonValue::as_number() const {
    if (type != NUMBER) throw std::runtime_error("expected number");
    return number_val;
}

int JsonValue::as_int() const { return static_cast<int>(as_number()); }

const std::string& JsonValue::as_string() const {
    if (type != STRING) throw std::runtime_error("expected string");
    return string_val;
}

bool JsonValue::as_bool() const {
    if (type != BOOL) throw std::runtime_error("expected bool");
    return bool_val;
}

const std::vector<JsonValue>& JsonValue::as_array() const {
    if (type != ARRAY) throw std::runtime_error("expected array");
    return array_val;
}

const std::map<std::string, JsonValue>& JsonValue::as_object() const {
    if (type != OBJECT) throw std::runtime_error("expected object");
    return object_val;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type != OBJECT) throw std::runtime_error("expected object");
    auto it = object_val.find(key);
    if (it == object_val.end())
        throw std::runtime_error("missing key: " + key);
    return it->second;
}

bool JsonValue::has(const std::string& key) const {
    return type == OBJECT && object_val.count(key) > 0;
}

double JsonValue::number_or(const std::string& key, double def) const {
    if (has(key)) return (*this)[key].as_number();
    return def;
}

int JsonValue::int_or(const std::string& key, int def) const {
    if (has(key)) return (*this)[key].as_int();
    return def;
}

const std::string& JsonValue::string_or(const std::string& key, const std::string& def) const {
    if (has(key)) return (*this)[key].as_string();
    return def;
}

// --- Parser ---

struct Parser {
    const std::string& input;
    size_t pos = 0;

    explicit Parser(const std::string& s) : input(s) {}

    void error(const std::string& msg) {
        throw std::runtime_error("JSON parse error at " + std::to_string(pos) + ": " + msg);
    }

    void skip_ws() {
        while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])))
            pos++;
    }

    char peek() {
        skip_ws();
        if (pos >= input.size()) error("unexpected end of input");
        return input[pos];
    }

    char advance() {
        char c = peek();
        pos++;
        return c;
    }

    void expect(char c) {
        char got = advance();
        if (got != c) error(std::string("expected '") + c + "', got '" + got + "'");
    }

    bool match(const char* s) {
        skip_ws();
        size_t len = std::strlen(s);
        if (pos + len > input.size()) return false;
        if (input.compare(pos, len, s) != 0) return false;
        pos += len;
        return true;
    }

    JsonValue parse_value() {
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        error(std::string("unexpected character '") + c + "'");
        return {}; // unreachable
    }

    JsonValue parse_null() {
        if (!match("null")) error("expected 'null'");
        JsonValue v;
        v.type = JsonValue::NUL;
        return v;
    }

    JsonValue parse_bool() {
        JsonValue v;
        v.type = JsonValue::BOOL;
        if (match("true")) { v.bool_val = true; return v; }
        if (match("false")) { v.bool_val = false; return v; }
        error("expected 'true' or 'false'");
        return {};
    }

    JsonValue parse_number() {
        skip_ws();
        size_t start = pos;
        if (pos < input.size() && input[pos] == '-') pos++;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) pos++;
        if (pos < input.size() && input[pos] == '.') {
            pos++;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) pos++;
        }
        if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
            pos++;
            if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) pos++;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) pos++;
        }
        if (pos == start) error("expected number");
        JsonValue v;
        v.type = JsonValue::NUMBER;
        v.number_val = std::strtod(input.c_str() + start, nullptr);
        return v;
    }

    JsonValue parse_string() {
        expect('"');
        std::string s;
        while (pos < input.size() && input[pos] != '"') {
            if (input[pos] == '\\') {
                pos++;
                if (pos >= input.size()) error("unterminated escape");
                switch (input[pos]) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    default: error("unknown escape");
                }
            } else {
                s += input[pos];
            }
            pos++;
        }
        if (pos >= input.size()) error("unterminated string");
        pos++; // closing quote
        JsonValue v;
        v.type = JsonValue::STRING;
        v.string_val = std::move(s);
        return v;
    }

    JsonValue parse_array() {
        expect('[');
        JsonValue v;
        v.type = JsonValue::ARRAY;
        if (peek() == ']') { pos++; return v; }
        v.array_val.push_back(parse_value());
        while (peek() == ',') {
            pos++;
            v.array_val.push_back(parse_value());
        }
        expect(']');
        return v;
    }

    JsonValue parse_object() {
        expect('{');
        JsonValue v;
        v.type = JsonValue::OBJECT;
        if (peek() == '}') { pos++; return v; }
        auto parse_pair = [&]() {
            JsonValue key = parse_string();
            expect(':');
            v.object_val[key.string_val] = parse_value();
        };
        parse_pair();
        while (peek() == ',') {
            pos++;
            parse_pair();
        }
        expect('}');
        return v;
    }
};

JsonValue json_parse(const std::string& input) {
    Parser p(input);
    JsonValue v = p.parse_value();
    p.skip_ws();
    if (p.pos != input.size())
        p.error("trailing content");
    return v;
}
