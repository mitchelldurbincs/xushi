#include "test_helpers.h"
#include "../src/json.h"
#include <cmath>

static void test_parse_number(TestContext& ctx) {
    auto v = json_parse("42");
    ctx.check(v.type == JsonValue::NUMBER && v.as_number() == 42.0, "parse integer");
    v = json_parse("-3.14");
    ctx.check(std::fabs(v.as_number() - (-3.14)) < 0.001, "parse negative float");
    v = json_parse("1e3");
    ctx.check(v.as_number() == 1000.0, "parse scientific notation");
}


static void test_number_grammar(TestContext& ctx) {
    bool caught = false;

    try { json_parse("-"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "reject bare minus");

    caught = false;
    try { json_parse("1e"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "reject exponent without digits");

    caught = false;
    try { json_parse("1e+"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "reject signed exponent without digits");

    caught = false;
    try { json_parse("-.5"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "reject missing integer part before decimal");

    caught = false;
    try { json_parse("01"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "reject leading zeros");

    ctx.check(json_parse("0").as_number() == 0.0, "accept zero");
    ctx.check(json_parse("-1").as_number() == -1.0, "accept negative integer");
    ctx.check(json_parse("1.0").as_number() == 1.0, "accept decimal number");
    ctx.check(std::fabs(json_parse("1e-3").as_number() - 1e-3) < 1e-12, "accept scientific notation with sign");
}
static void test_parse_string(TestContext& ctx) {
    auto v = json_parse("\"hello\"");
    ctx.check(v.as_string() == "hello", "parse simple string");
    v = json_parse("\"a\\nb\"");
    ctx.check(v.as_string() == "a\nb", "parse string with escape");
}

static void test_parse_bool_null(TestContext& ctx) {
    ctx.check(json_parse("true").as_bool() == true, "parse true");
    ctx.check(json_parse("false").as_bool() == false, "parse false");
    ctx.check(json_parse("null").type == JsonValue::NUL, "parse null");
}

static void test_parse_array(TestContext& ctx) {
    auto v = json_parse("[1, 2, 3]");
    ctx.check(v.as_array().size() == 3, "array size");
    ctx.check(v.as_array()[0].as_number() == 1.0, "array element 0");
    ctx.check(v.as_array()[2].as_number() == 3.0, "array element 2");
}

static void test_parse_object(TestContext& ctx) {
    auto v = json_parse("{\"a\": 1, \"b\": \"two\"}");
    ctx.check(v["a"].as_number() == 1.0, "object number value");
    ctx.check(v["b"].as_string() == "two", "object string value");
    ctx.check(v.has("a"), "has existing key");
    ctx.check(!v.has("c"), "missing key");
}

static void test_nested(TestContext& ctx) {
    auto v = json_parse("{\"arr\": [1, {\"x\": 2}]}");
    ctx.check(v["arr"].as_array()[1]["x"].as_number() == 2.0, "nested access");
}

static void test_empty_containers(TestContext& ctx) {
    ctx.check(json_parse("[]").as_array().empty(), "empty array");
    ctx.check(json_parse("{}").as_object().empty(), "empty object");
}

static void test_parse_error(TestContext& ctx) {
    bool caught = false;
    try { json_parse("{invalid}"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "parse error on malformed input");
    caught = false;
    try { json_parse("42 extra"); } catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "parse error on trailing content");
}

static void test_defaults(TestContext& ctx) {
    auto v = json_parse("{\"a\": 5}");
    ctx.check(v.number_or("a", 0.0) == 5.0, "number_or existing key");
    ctx.check(v.number_or("b", 99.0) == 99.0, "number_or missing key");
    ctx.check(v.int_or("b", 7) == 7, "int_or missing key");
}

int main() {
    return run_test_suite("JSON", [](TestContext& ctx) {
    test_parse_number(ctx);
    test_number_grammar(ctx);
    test_parse_string(ctx);
    test_parse_bool_null(ctx);
    test_parse_array(ctx);
    test_parse_object(ctx);
    test_nested(ctx);
    test_empty_containers(ctx);
    test_parse_error(ctx);
    test_defaults(ctx);
    });
}
