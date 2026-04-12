#include "test_helpers.h"
#include "../src/scenario.h"
#include <cmath>
#include <fstream>

static const char* kInvalidScenarioPath = "tests/tmp_invalid_scenario.json";

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

static std::string write_invalid_scenario(const std::string& body) {
    std::ofstream out(kInvalidScenarioPath);
    out << "{\n"
        << "  \"seed\": 123,\n"
        << "  \"ticks\": 10,\n"
        << "  \"dt\": 1.0,\n"
        << "  \"max_sensor_range\": 50.0,\n"
        << "  \"channel\": {\"base_latency\": 3, \"per_distance\": 0.0, \"loss\": 0.1},\n"
        << "  \"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": 0.05},\n"
        << "  \"obstacles\": [],\n"
        << "  \"entities\": [\n"
        << "    {\"id\": 1, \"type\": \"target\", \"pos\": [0, 0], \"vel\": [0, 0]}\n"
        << "  ],\n"
        << body << "\n"
        << "}\n";
    return kInvalidScenarioPath;
}

static void check_invalid_field(const std::string& name,
                                const std::string& override_field,
                                const std::string& expected_message_part) {
    const std::string path = write_invalid_scenario(override_field);
    bool caught = false;
    std::string message;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = true;
        message = e.what();
    }

    CHECK(caught, name + " throws");
    CHECK(message.find(path) != std::string::npos, name + " includes path");
    CHECK(message.find(expected_message_part) != std::string::npos, name + " includes detail");
}

static void test_invalid_validations() {
    check_invalid_field("ticks", "\"ticks\": -1", "ticks must be >= 0, got -1");
    check_invalid_field("dt", "\"dt\": 0", "dt must be > 0, got 0");
    check_invalid_field("max_sensor_range", "\"max_sensor_range\": 0", "max_sensor_range must be > 0, got 0");
    check_invalid_field("channel.base_latency_ticks", "\"channel\": {\"base_latency\": -1, \"per_distance\": 0.0, \"loss\": 0.1}", "channel.base_latency_ticks must be >= 0, got -1");
    check_invalid_field("channel.latency_per_distance", "\"channel\": {\"base_latency\": 3, \"per_distance\": -0.5, \"loss\": 0.1}", "channel.latency_per_distance must be >= 0, got -0.5");
    check_invalid_field("channel.loss_probability", "\"channel\": {\"base_latency\": 3, \"per_distance\": 0.0, \"loss\": 1.5}", "channel.loss_probability must be in [0, 1], got 1.5");
    check_invalid_field("belief.fresh_ticks", "\"belief\": {\"fresh_ticks\": -1, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": 0.05}", "belief.fresh_ticks must be >= 0, got -1");
    check_invalid_field("belief.stale_ticks", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": -1, \"uncertainty_growth\": 0.5, \"confidence_decay\": 0.05}", "belief.stale_ticks must be >= 0, got -1");
    check_invalid_field("belief.uncertainty_growth_rate", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": -0.1, \"confidence_decay\": 0.05}", "belief.uncertainty_growth_rate must be >= 0, got -0.1");
    check_invalid_field("belief.confidence_decay_rate", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": -0.05}", "belief.confidence_decay_rate must be >= 0, got -0.05");
}

int main() {
    std::printf("Running scenario tests...\n");
    test_load_default();
    test_load_los_blocked();
    test_missing_file();
    test_invalid_validations();
    TEST_REPORT();
}
