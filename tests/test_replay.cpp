#include "test_helpers.h"
#include "../src/belief.h"
#include "../src/replay.h"
#include "../src/replay_events.h"
#include "../src/scenario.h"

#include <cstdio>

static const char* TEST_FILE = "test_output.replay";

static void test_write_read_roundtrip(TestContext& ctx) {
    {
        ReplayWriter w(TEST_FILE);
        w.log(json_object({{"type", json_string("test_event")},
                           {"round", json_number(0)},
                           {"value", json_number(42)}}));
        w.log(json_object({{"type", json_string("test_event")},
                           {"round", json_number(1)},
                           {"value", json_number(99)}}));
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
    scn.rounds = 12;
    scn.ascii_map = {"..", ".."};
    scn.grid = GridMap::from_ascii(scn.ascii_map);
    auto hdr = replay_header(scn, "test.json");
    ctx.check(hdr["type"].as_string() == "header", "header: type");
    ctx.check(hdr["seed"].as_number() == 42, "header: seed");
    ctx.check(hdr["scenario"].as_string() == "test.json", "header: scenario path");
    ctx.check(hdr["rounds"].as_number() == 12, "header: rounds");
}

static void test_track_update_event(TestContext& ctx) {
    Track t;
    t.target = 7;
    t.estimated_position = {3, 4};
    t.confidence = 0.75f;
    t.uncertainty = 1.5f;
    t.status = TrackStatus::FRESH;
    t.class_id = 2;
    auto ev = replay_track_update(5, /*team*/1, t);
    ctx.check(ev["type"].as_string() == "track_update", "track_update: type");
    ctx.check(ev["round"].as_number() == 5, "track_update: round");
    ctx.check(ev["target"].as_number() == 7, "track_update: target");
    ctx.check(ev["status"].as_string() == "FRESH", "track_update: status");
    ctx.check(ev["pos"].as_array().size() == 2, "track_update: pos array");
}

static void test_world_hash_event(TestContext& ctx) {
    auto ev = replay_world_hash(10, 0xDEADBEEF12345678ULL);
    ctx.check(ev["type"].as_string() == "world_hash", "world_hash: type");
    ctx.check(ev["round"].as_number() == 10, "world_hash: round");
    ctx.check(ev["hash"].as_string() == "deadbeef12345678", "world_hash: hex");
}

int main() {
    return run_test_suite("replay", [](TestContext& ctx) {
        test_write_read_roundtrip(ctx);
        test_filter_by_type(ctx);
        test_header_event(ctx);
        test_track_update_event(ctx);
        test_world_hash_event(ctx);
        std::remove(TEST_FILE);
    });
}
