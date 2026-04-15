#include "test_helpers.h"
#include "../src/scenario.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

static const char* TEST_PATH = "test_scenario.tmp.json";

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

static void test_loads_small_office_breach(TestContext& ctx) {
    Scenario s = load_scenario("scenarios/small_office_breach.json");
    ctx.check(s.seed == 1103, "seed parsed");
    ctx.check(s.rounds == 12, "rounds parsed");
    ctx.check(s.grid.width() == 16 && s.grid.height() == 12,
              "grid dims 16x12");
    ctx.check(s.entities.size() == 6, "6 entities (4 operators + 2 drones)");
    ctx.check(s.devices.size() == 3, "3 devices");
    ctx.check(s.game_mode.type == "office_breach", "office_breach game mode");
    ctx.check(s.game_mode.objective_cell == GridPos{13, 3}, "objective parsed");
    ctx.check(s.grid.doors().size() == 1, "1 door parsed");
    ctx.check(s.grid.room_count() >= 2, "objective room separated by CLOSED door");
}

static void test_minimal_valid_scenario(TestContext& ctx) {
    const std::string json = R"({
  "seed": 1,
  "rounds": 3,
  "map": { "rows": ["WWW", "W.W", "WWW"] },
  "entities": [
    { "id": 0, "kind": "operator", "pos": [1, 1], "team": 0 }
  ]
})";
    write_file(TEST_PATH, json);
    Scenario s = load_scenario(TEST_PATH);
    ctx.check(s.entities.size() == 1, "one entity");
    ctx.check(s.entities[0].pos == GridPos{1, 1}, "pos parsed");
    ctx.check(s.entities[0].kind == EntityKind::Operator, "operator kind");
    std::remove(TEST_PATH);
}

static void test_rejects_out_of_bounds(TestContext& ctx) {
    const std::string json = R"({
  "seed": 1,
  "rounds": 3,
  "map": { "rows": ["WWW", "W.W", "WWW"] },
  "entities": [
    { "id": 0, "kind": "operator", "pos": [5, 5], "team": 0 }
  ]
})";
    write_file(TEST_PATH, json);
    bool threw = false;
    try { load_scenario(TEST_PATH); }
    catch (const std::runtime_error&) { threw = true; }
    ctx.check(threw, "throws on out-of-bounds entity");
    std::remove(TEST_PATH);
}

static void test_rejects_duplicate_id(TestContext& ctx) {
    const std::string json = R"({
  "seed": 1,
  "rounds": 3,
  "map": { "rows": ["WWW", "W.W", "WWW"] },
  "entities": [
    { "id": 0, "kind": "operator", "pos": [1, 1], "team": 0 },
    { "id": 0, "kind": "operator", "pos": [1, 1], "team": 1 }
  ]
})";
    write_file(TEST_PATH, json);
    bool threw = false;
    try { load_scenario(TEST_PATH); }
    catch (const std::runtime_error&) { threw = true; }
    ctx.check(threw, "throws on duplicate entity id");
    std::remove(TEST_PATH);
}

int main() {
    return run_test_suite("scenario", [](TestContext& ctx) {
        test_loads_small_office_breach(ctx);
        test_minimal_valid_scenario(ctx);
        test_rejects_out_of_bounds(ctx);
        test_rejects_duplicate_id(ctx);
    });
}
