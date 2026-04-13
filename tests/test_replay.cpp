#include "test_helpers.h"
#include "../src/replay.h"
#include "../src/replay_events.h"
#include "../src/scenario.h"
#include <cstdio>

static const char* TEST_FILE = "test_output.replay";

static void test_write_read_roundtrip(TestContext& ctx) {
    {
        ReplayWriter w(TEST_FILE);
        w.log(json_object({{"type", json_string("test_event")}, {"tick", json_number(0)}, {"value", json_number(42)}}));
        w.log(json_object({{"type", json_string("test_event")}, {"tick", json_number(1)}, {"value", json_number(99)}}));
    }
    ReplayReader r(TEST_FILE);
    auto events = r.read_all();
    ctx.check(events.size() == 2, "roundtrip: two events read back");
    ctx.check(events[0]["value"].as_number() == 42, "roundtrip: first value correct");
    ctx.check(events[1]["value"].as_number() == 99, "roundtrip: second value correct");
}

static void test_filter_by_type(TestContext& ctx) {
    {
        ReplayWriter w(TEST_FILE);
        w.log(json_object({{"type", json_string("alpha")}, {"x", json_number(1)}}));
        w.log(json_object({{"type", json_string("beta")},  {"x", json_number(2)}}));
        w.log(json_object({{"type", json_string("alpha")}, {"x", json_number(3)}}));
        w.log(json_object({{"type", json_string("beta")},  {"x", json_number(4)}}));
    }
    ReplayReader r(TEST_FILE);
    auto alphas = r.filter("alpha");
    ctx.check(alphas.size() == 2, "filter: two alpha events");
    ctx.check(alphas[0]["x"].as_number() == 1, "filter: first alpha value");
    ctx.check(alphas[1]["x"].as_number() == 3, "filter: second alpha value");
}

static void test_header_event(TestContext& ctx) {
    Scenario scn;
    scn.seed = 42;
    scn.dt = 0.5f;
    scn.ticks = 100;
    auto hdr = replay_header(scn, "test.json");
    ctx.check(hdr["type"].as_string() == "header", "header: type");
    ctx.check(hdr["seed"].as_number() == 42, "header: seed");
    ctx.check(hdr["scenario"].as_string() == "test.json", "header: scenario path");
}

static void test_detection_event(TestContext& ctx) {
    Observation obs = {5, 0, 2, {10.5f, 20.3f}, 1.2f, 0.8f};
    auto ev = replay_detection(5, obs);
    ctx.check(ev["type"].as_string() == "detection", "detection: type");
    ctx.check(ev["tick"].as_number() == 5, "detection: tick");
    ctx.check(ev["target"].as_number() == 2, "detection: target");
    ctx.check(ev["est_pos"].as_array().size() == 2, "detection: est_pos array");
}

static void test_world_hash_event(TestContext& ctx) {
    auto ev = replay_world_hash(10, 0xDEADBEEF12345678ULL);
    ctx.check(ev["type"].as_string() == "world_hash", "world_hash: type");
    ctx.check(ev["tick"].as_number() == 10, "world_hash: tick");
    ctx.check(ev["hash"].as_string() == "deadbeef12345678", "world_hash: hex value");
}

static void test_serialization_roundtrip(TestContext& ctx) {
    Observation obs = {3, 1, 2, {5.0f, 10.0f}, 0.5f, 0.9f};
    auto original = replay_detection(3, obs);
    std::string line = json_serialize(original);
    auto parsed = json_parse(line);
    ctx.check(parsed["type"].as_string() == "detection", "serde: type survives");
    ctx.check(parsed["tick"].as_number() == 3, "serde: tick survives");
    ctx.check(parsed["target"].as_number() == 2, "serde: target survives");
}

int main() {
    return run_test_suite("replay", [](TestContext& ctx) {
    test_write_read_roundtrip(ctx);
    test_filter_by_type(ctx);
    test_header_event(ctx);
    test_detection_event(ctx);
    test_world_hash_event(ctx);
    test_serialization_roundtrip(ctx);
    std::remove(TEST_FILE);
    });
}
