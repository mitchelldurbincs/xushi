#include "test_helpers.h"
#include "../src/replay.h"
#include "../src/replay_events.h"
#include "../src/scenario.h"
#include <cstdlib>

static const char* TEST_FILE = "test_output.replay";

static void test_write_read_roundtrip() {
    {
        ReplayWriter w(TEST_FILE);
        w.log(json_object({{"type", json_string("test_event")}, {"tick", json_number(0)}, {"value", json_number(42)}}));
        w.log(json_object({{"type", json_string("test_event")}, {"tick", json_number(1)}, {"value", json_number(99)}}));
    }
    ReplayReader r(TEST_FILE);
    auto events = r.read_all();
    CHECK(events.size() == 2, "roundtrip: two events read back");
    CHECK(events[0]["value"].as_number() == 42, "roundtrip: first value correct");
    CHECK(events[1]["value"].as_number() == 99, "roundtrip: second value correct");
}

static void test_filter_by_type() {
    {
        ReplayWriter w(TEST_FILE);
        w.log(json_object({{"type", json_string("alpha")}, {"x", json_number(1)}}));
        w.log(json_object({{"type", json_string("beta")},  {"x", json_number(2)}}));
        w.log(json_object({{"type", json_string("alpha")}, {"x", json_number(3)}}));
        w.log(json_object({{"type", json_string("beta")},  {"x", json_number(4)}}));
    }
    ReplayReader r(TEST_FILE);
    auto alphas = r.filter("alpha");
    CHECK(alphas.size() == 2, "filter: two alpha events");
    CHECK(alphas[0]["x"].as_number() == 1, "filter: first alpha value");
    CHECK(alphas[1]["x"].as_number() == 3, "filter: second alpha value");
}

static void test_header_event() {
    Scenario scn;
    scn.seed = 42;
    scn.dt = 0.5f;
    scn.ticks = 100;
    auto hdr = replay_header(scn, "test.json");
    CHECK(hdr["type"].as_string() == "header", "header: type");
    CHECK(hdr["seed"].as_number() == 42, "header: seed");
    CHECK(hdr["scenario"].as_string() == "test.json", "header: scenario path");
}

static void test_detection_event() {
    Observation obs = {5, 0, 2, {10.5f, 20.3f}, 1.2f, 0.8f};
    auto ev = replay_detection(5, obs);
    CHECK(ev["type"].as_string() == "detection", "detection: type");
    CHECK(ev["tick"].as_number() == 5, "detection: tick");
    CHECK(ev["target"].as_number() == 2, "detection: target");
    CHECK(ev["est_pos"].as_array().size() == 2, "detection: est_pos array");
}

static void test_world_hash_event() {
    auto ev = replay_world_hash(10, 0xDEADBEEF12345678ULL);
    CHECK(ev["type"].as_string() == "world_hash", "world_hash: type");
    CHECK(ev["tick"].as_number() == 10, "world_hash: tick");
    CHECK(ev["hash"].as_string() == "deadbeef12345678", "world_hash: hex value");
}

static void test_serialization_roundtrip() {
    Observation obs = {3, 1, 2, {5.0f, 10.0f}, 0.5f, 0.9f};
    auto original = replay_detection(3, obs);
    std::string line = json_serialize(original);
    auto parsed = json_parse(line);
    CHECK(parsed["type"].as_string() == "detection", "serde: type survives");
    CHECK(parsed["tick"].as_number() == 3, "serde: tick survives");
    CHECK(parsed["target"].as_number() == 2, "serde: target survives");
}

int main() {
    std::printf("Running replay tests...\n");
    test_write_read_roundtrip();
    test_filter_by_type();
    test_header_event();
    test_detection_event();
    test_world_hash_event();
    test_serialization_roundtrip();
    std::remove(TEST_FILE);
    TEST_REPORT();
}
