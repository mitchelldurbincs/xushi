#include "test_helpers.h"
#include "../src/json.h"
#include <cmath>

static void test_parse_number() {
    auto v = json_parse("42");
    CHECK(v.type == JsonValue::NUMBER && v.as_number() == 42.0, "parse integer");
    v = json_parse("-3.14");
    CHECK(std::fabs(v.as_number() - (-3.14)) < 0.001, "parse negative float");
    v = json_parse("1e3");
    CHECK(v.as_number() == 1000.0, "parse scientific notation");
}


static void test_number_grammar() {
    bool caught = false;

    try { json_parse("-"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "reject bare minus");

    caught = false;
    try { json_parse("1e"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "reject exponent without digits");

    caught = false;
    try { json_parse("1e+"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "reject signed exponent without digits");

    caught = false;
    try { json_parse("-.5"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "reject missing integer part before decimal");

    caught = false;
    try { json_parse("01"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "reject leading zeros");

    CHECK(json_parse("0").as_number() == 0.0, "accept zero");
    CHECK(json_parse("-1").as_number() == -1.0, "accept negative integer");
    CHECK(json_parse("1.0").as_number() == 1.0, "accept decimal number");
    CHECK(std::fabs(json_parse("1e-3").as_number() - 1e-3) < 1e-12, "accept scientific notation with sign");
}
static void test_parse_string() {
    auto v = json_parse("\"hello\"");
    CHECK(v.as_string() == "hello", "parse simple string");
    v = json_parse("\"a\\nb\"");
    CHECK(v.as_string() == "a\nb", "parse string with escape");
}

static void test_parse_bool_null() {
    CHECK(json_parse("true").as_bool() == true, "parse true");
    CHECK(json_parse("false").as_bool() == false, "parse false");
    CHECK(json_parse("null").type == JsonValue::NUL, "parse null");
}

static void test_parse_array() {
    auto v = json_parse("[1, 2, 3]");
    CHECK(v.as_array().size() == 3, "array size");
    CHECK(v.as_array()[0].as_number() == 1.0, "array element 0");
    CHECK(v.as_array()[2].as_number() == 3.0, "array element 2");
}

static void test_parse_object() {
    auto v = json_parse("{\"a\": 1, \"b\": \"two\"}");
    CHECK(v["a"].as_number() == 1.0, "object number value");
    CHECK(v["b"].as_string() == "two", "object string value");
    CHECK(v.has("a"), "has existing key");
    CHECK(!v.has("c"), "missing key");
}

static void test_nested() {
    auto v = json_parse("{\"arr\": [1, {\"x\": 2}]}");
    CHECK(v["arr"].as_array()[1]["x"].as_number() == 2.0, "nested access");
}

static void test_empty_containers() {
    CHECK(json_parse("[]").as_array().empty(), "empty array");
    CHECK(json_parse("{}").as_object().empty(), "empty object");
}

static void test_parse_error() {
    bool caught = false;
    try { json_parse("{invalid}"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "parse error on malformed input");
    caught = false;
    try { json_parse("42 extra"); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "parse error on trailing content");
}

static void test_defaults() {
    auto v = json_parse("{\"a\": 5}");
    CHECK(v.number_or("a", 0.0) == 5.0, "number_or existing key");
    CHECK(v.number_or("b", 99.0) == 99.0, "number_or missing key");
    CHECK(v.int_or("b", 7) == 7, "int_or missing key");
}

int main() {
    std::printf("Running JSON tests...\n");
    test_parse_number();
    test_number_grammar();
    test_parse_string();
    test_parse_bool_null();
    test_parse_array();
    test_parse_object();
    test_nested();
    test_empty_containers();
    test_parse_error();
    test_defaults();
    TEST_REPORT();
}
