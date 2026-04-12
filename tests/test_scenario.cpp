#include "test_helpers.h"
#include "../src/scenario.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

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

int main() {
    std::printf("Running scenario tests...\n");
    test_load_default();
    test_load_los_blocked();
    test_missing_file();
    test_duplicate_drone_rejected();
    test_duplicate_ground_rejected();
    test_unknown_role_rejected();
    TEST_REPORT();
}
