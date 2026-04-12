#include "test_helpers.h"
#include "../src/scenario.h"
#include <cmath>
#include <cstdio>
#include <fstream>

static void test_load_default() {
    Scenario s = load_scenario("scenarios/default.json");
    CHECK(s.seed == 12345, "seed");
    CHECK(s.dt == 1.0f, "dt");
    CHECK(s.ticks == 60, "ticks");
    CHECK(std::fabs(s.max_sensor_range - 80.0f) < 0.01f, "max_sensor_range");
    CHECK(s.obstacles.size() == 1, "obstacle count");
    CHECK(s.obstacles[0].min.x == 45.0f, "obstacle min.x");
    CHECK(s.entities.size() == 3, "entity count");
    CHECK(s.entities[0].type == "drone", "first entity type");
    CHECK(s.entities[2].velocity.x == 1.0f, "target velocity");
    CHECK(s.channel.base_latency_ticks == 3, "channel base_latency");
    CHECK(std::fabs(s.channel.loss_probability - 0.1f) < 0.01f, "channel loss");
    CHECK(s.belief.fresh_ticks == 5, "belief fresh_ticks");
}

static void test_load_los_blocked() {
    Scenario s = load_scenario("scenarios/los_blocked.json");
    CHECK(s.seed == 99, "seed");
    CHECK(s.ticks == 20, "ticks");
    CHECK(s.entities[2].velocity.x == 0.0f, "stationary target");
}

static void test_missing_file() {
    bool caught = false;
    try { load_scenario("nonexistent.json"); }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught, "error on missing file");
}

static void test_belief_rate_units_per_second_keys() {
    const char* path = "scenarios/__tmp_belief_units_test.json";
    std::ofstream out(path);
    out << "{\n"
        << "  \"seed\": 7,\n"
        << "  \"dt\": 0.5,\n"
        << "  \"ticks\": 2,\n"
        << "  \"obstacles\": [],\n"
        << "  \"entities\": [\n"
        << "    {\"id\": 0, \"type\": \"drone\", \"pos\": [0, 0], \"vel\": [0, 0]},\n"
        << "    {\"id\": 1, \"type\": \"ground\", \"pos\": [0, 0], \"vel\": [0, 0]},\n"
        << "    {\"id\": 2, \"type\": \"target\", \"pos\": [1, 0], \"vel\": [0, 0]}\n"
        << "  ],\n"
        << "  \"belief\": {\n"
        << "    \"fresh_ticks\": 3,\n"
        << "    \"stale_ticks\": 4,\n"
        << "    \"uncertainty_growth_per_second\": 1.25,\n"
        << "    \"confidence_decay_per_second\": 0.75\n"
        << "  }\n"
        << "}\n";
    out.close();

    Scenario s = load_scenario(path);
    CHECK(std::fabs(s.belief.uncertainty_growth_per_second - 1.25f) < 0.0001f,
          "loads uncertainty growth per second");
    CHECK(std::fabs(s.belief.confidence_decay_per_second - 0.75f) < 0.0001f,
          "loads confidence decay per second");

    std::remove(path);
}

int main() {
    std::printf("Running scenario tests...\n");
    test_load_default();
    test_load_los_blocked();
    test_missing_file();
    test_belief_rate_units_per_second_keys();
    TEST_REPORT();
}
