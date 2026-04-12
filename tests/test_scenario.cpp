#include "test_helpers.h"
#include "../src/scenario.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

static const char* kInvalidScenarioPath = "tests/tmp_invalid_scenario.json";

static void write_temp_scenario(const char* path, const char* json) {
    std::ofstream out(path);
    out << json;
}

static void test_load_default() {
    Scenario s = load_scenario("scenarios/default.json");
    CHECK(s.seed == 12345, "seed");
    CHECK(s.dt == 1.0f, "dt");
    CHECK(s.ticks == 60, "ticks");
    CHECK(std::fabs(s.max_sensor_range - 80.0f) < 0.01f, "max_sensor_range");
    CHECK(s.obstacles.size() == 1, "obstacle count");
    CHECK(s.obstacles[0].min.x == 45.0f, "obstacle min.x");
    CHECK(s.entities.size() == 3, "entity count");
    CHECK(s.entities[0].role == ScenarioEntity::Role::Drone, "first entity role");
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

static void test_duplicate_drone_rejected() {
    const char* path = "tests/tmp_duplicate_drone.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0]},"
        "{\"id\":1,\"type\":\"drone\",\"pos\":[1,0],\"vel\":[0,0]},"
        "{\"id\":2,\"type\":\"ground\",\"pos\":[0,1],\"vel\":[0,0]},"
        "{\"id\":3,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0]}"
        "]"
        "}");

    bool caught = false;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("duplicate role 'drone'") != std::string::npos;
    }
    CHECK(caught, "duplicate drone rejected");
    std::remove(path);
}

static void test_duplicate_ground_rejected() {
    const char* path = "tests/tmp_duplicate_ground.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0]},"
        "{\"id\":1,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0]},"
        "{\"id\":2,\"type\":\"ground\",\"pos\":[0,1],\"vel\":[0,0]},"
        "{\"id\":3,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0]}"
        "]"
        "}");

    bool caught = false;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("duplicate role 'ground'") != std::string::npos;
    }
    CHECK(caught, "duplicate ground rejected");
    std::remove(path);
}

static void test_unknown_role_rejected() {
    const char* path = "tests/tmp_unknown_role.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0]},"
        "{\"id\":1,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0]},"
        "{\"id\":2,\"type\":\"alien\",\"pos\":[5,5],\"vel\":[0,0]}"
        "]"
        "}");

    bool caught = false;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("unknown entity role 'alien'") != std::string::npos;
    }
    CHECK(caught, "unknown role rejected");
    std::remove(path);
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
        << "    {\"id\": 1, \"type\": \"drone\", \"pos\": [0, 0], \"vel\": [0, 0]},\n"
        << "    {\"id\": 2, \"type\": \"ground\", \"pos\": [5, 5], \"vel\": [0, 0]},\n"
        << "    {\"id\": 3, \"type\": \"target\", \"pos\": [10, 10], \"vel\": [0, 0]}\n"
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

    CHECK(caught, (name + " throws").c_str());
    CHECK(message.find(path) != std::string::npos, (name + " includes path").c_str());
    CHECK(message.find(expected_message_part) != std::string::npos, (name + " includes detail").c_str());
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
    check_invalid_field("belief.uncertainty_growth_per_second", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": -0.1, \"confidence_decay\": 0.05}", "belief.uncertainty_growth_per_second must be >= 0, got -0.1");
    check_invalid_field("belief.confidence_decay_per_second", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": -0.05}", "belief.confidence_decay_per_second must be >= 0, got -0.05");
}

int main() {
    std::printf("Running scenario tests...\n");
    test_load_default();
    test_load_los_blocked();
    test_missing_file();
    test_duplicate_drone_rejected();
    test_duplicate_ground_rejected();
    test_unknown_role_rejected();
    test_belief_rate_units_per_second_keys();
    test_invalid_validations();
    TEST_REPORT();
}
