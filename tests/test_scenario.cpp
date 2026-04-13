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

static void test_load_default(TestContext& ctx) {
    Scenario s = load_scenario("scenarios/default.json");
    ctx.check(s.seed == 12345, "seed");
    ctx.check(s.dt == 1.0f, "dt");
    ctx.check(s.ticks == 60, "ticks");
    ctx.check(std::fabs(s.max_sensor_range - 80.0f) < 0.01f, "max_sensor_range");
    ctx.check(s.obstacles.size() == 1, "obstacle count");
    ctx.check(s.obstacles[0].min.x == 45.0f, "obstacle min.x");
    ctx.check(s.entities.size() == 3, "entity count");
    ctx.check(s.entities[0].role_name == "drone", "first entity role_name");
    ctx.check(s.entities[0].can_sense == true, "drone can_sense");
    ctx.check(s.entities[0].can_engage == true, "drone can_engage");
    ctx.check(s.entities[0].ammo == 6, "drone ammo");
    ctx.check(s.entities[0].allowed_effect_profile_indices.size() == 1,
          "drone allowed profile count");
    ctx.check(s.entities[0].allowed_effect_profile_indices[0] == 0,
          "drone allowed profile index");
    ctx.check(s.entities[2].velocity.x == 1.0f, "target velocity");
    ctx.check(s.effect_profiles.size() == 1, "effect profile count");
    ctx.check(s.effect_profiles[0].name == "standard", "effect profile name");
    ctx.check(std::fabs(s.effect_profiles[0].range - 60.0f) < 0.01f,
          "effect profile range");
    ctx.check(s.channel.base_latency_ticks == 3, "channel base_latency");
    ctx.check(std::fabs(s.channel.loss_probability - 0.1f) < 0.01f, "channel loss");
    ctx.check(s.belief.fresh_ticks == 5, "belief fresh_ticks");
}

static void test_load_los_blocked(TestContext& ctx) {
    Scenario s = load_scenario("scenarios/los_blocked.json");
    ctx.check(s.seed == 99, "seed");
    ctx.check(s.ticks == 20, "ticks");
    ctx.check(s.entities[2].velocity.x == 0.0f, "stationary target");
}

static void test_missing_file(TestContext& ctx) {
    bool caught = false;
    try { load_scenario("nonexistent.json"); }
    catch (const std::runtime_error&) { caught = true; }
    ctx.check(caught, "error on missing file");
}

static void test_multi_drone_accepted(TestContext& ctx) {
    const char* path = "tests/tmp_multi_drone.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true},"
        "{\"id\":1,\"type\":\"drone\",\"pos\":[1,0],\"vel\":[0,0],\"can_sense\":true},"
        "{\"id\":2,\"type\":\"ground\",\"pos\":[0,1],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":3,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0],\"is_observable\":true}"
        "]"
        "}");

    bool ok = false;
    try {
        Scenario s = load_scenario(path);
        ok = s.entities.size() == 4;
    } catch (...) {}
    ctx.check(ok, "multi drone accepted");
    std::remove(path);
}

static void test_multi_ground_accepted(TestContext& ctx) {
    const char* path = "tests/tmp_multi_ground.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true},"
        "{\"id\":1,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":2,\"type\":\"ground\",\"pos\":[0,1],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":3,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0],\"is_observable\":true}"
        "]"
        "}");

    bool ok = false;
    try {
        Scenario s = load_scenario(path);
        ok = s.entities.size() == 4;
    } catch (...) {}
    ctx.check(ok, "multi ground accepted");
    std::remove(path);
}

static void test_custom_role_accepted(TestContext& ctx) {
    const char* path = "tests/tmp_custom_role.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true},"
        "{\"id\":1,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":2,\"type\":\"scout\",\"pos\":[5,5],\"vel\":[0,0],"
        " \"can_sense\":true,\"is_observable\":true}"
        "]"
        "}");

    Scenario s = load_scenario(path);
    ctx.check(s.entities[2].role_name == "scout", "scout role_name");
    ctx.check(s.entities[2].can_sense == true, "scout can_sense");
    ctx.check(s.entities[2].is_observable == true, "scout is_observable");
    ctx.check(s.entities[2].can_track == false, "scout !can_track");
    std::remove(path);
}

static void test_capability_override(TestContext& ctx) {
    const char* path = "tests/tmp_cap_override.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true,\"can_track\":true},"
        "{\"id\":1,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0],\"is_observable\":true}"
        "]"
        "}");

    Scenario s = load_scenario(path);
    ctx.check(s.entities[0].can_sense == true, "drone keeps can_sense");
    ctx.check(s.entities[0].can_track == true, "drone override can_track");
    std::remove(path);
}

static void test_missing_capabilities_rejected(TestContext& ctx) {
    const char* path = "tests/tmp_no_caps.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"blob\",\"pos\":[0,0],\"vel\":[0,0]},"
        "{\"id\":1,\"type\":\"blob\",\"pos\":[1,0],\"vel\":[0,0]}"
        "]"
        "}");

    bool caught = false;
    try {
        load_scenario(path);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    ctx.check(caught, "no capabilities rejected");
    std::remove(path);
}

static void test_duplicate_entity_ids_rejected(TestContext& ctx) {
    const char* path = "tests/tmp_duplicate_ids.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"entities\":["
        "{\"id\":7,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true},"
        "{\"id\":7,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":3,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0],\"is_observable\":true}"
        "]"
        "}");

    bool caught = false;
    std::string message;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = true;
        message = e.what();
    }

    ctx.check(caught, "duplicate ids rejected");
    ctx.check(message.find(path) != std::string::npos, "duplicate id message includes path");
    ctx.check(message.find("duplicate entity id 7") != std::string::npos,
          "duplicate id message includes id");
    std::remove(path);
}

static void test_belief_rate_units_per_second_keys(TestContext& ctx) {
    const char* path = "scenarios/__tmp_belief_units_test.json";
    std::ofstream out(path);
    out << "{\n"
        << "  \"seed\": 7,\n"
        << "  \"dt\": 0.5,\n"
        << "  \"ticks\": 2,\n"
        << "  \"obstacles\": [],\n"
        << "  \"entities\": [\n"
        << "    {\"id\": 0, \"type\": \"drone\", \"pos\": [0, 0], \"vel\": [0, 0], \"can_sense\": true},\n"
        << "    {\"id\": 1, \"type\": \"ground\", \"pos\": [0, 0], \"vel\": [0, 0], \"can_track\": true},\n"
        << "    {\"id\": 2, \"type\": \"target\", \"pos\": [1, 0], \"vel\": [0, 0], \"is_observable\": true}\n"
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
    ctx.check(std::fabs(s.belief.uncertainty_growth_per_second - 1.25f) < 0.0001f,
          "loads uncertainty growth per second");
    ctx.check(std::fabs(s.belief.confidence_decay_per_second - 0.75f) < 0.0001f,
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
        << "    {\"id\": 1, \"type\": \"drone\", \"pos\": [0, 0], \"vel\": [0, 0], \"can_sense\": true},\n"
        << "    {\"id\": 2, \"type\": \"ground\", \"pos\": [5, 5], \"vel\": [0, 0], \"can_track\": true},\n"
        << "    {\"id\": 3, \"type\": \"target\", \"pos\": [10, 10], \"vel\": [0, 0], \"is_observable\": true}\n"
        << "  ],\n"
        << body << "\n"
        << "}\n";
    return kInvalidScenarioPath;
}

static void check_invalid_field(TestContext& ctx,
                                const std::string& name,
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

    ctx.check(caught, (name + " throws").c_str());
    ctx.check(message.find(path) != std::string::npos, (name + " includes path").c_str());
    ctx.check(message.find(expected_message_part) != std::string::npos, (name + " includes detail").c_str());
}

static void test_invalid_validations(TestContext& ctx) {
    check_invalid_field(ctx, "ticks", "\"ticks\": -1", "ticks must be >= 0, got -1");
    check_invalid_field(ctx, "dt", "\"dt\": 0", "dt must be > 0, got 0");
    check_invalid_field(ctx, "max_sensor_range", "\"max_sensor_range\": 0", "max_sensor_range must be > 0, got 0");
    check_invalid_field(ctx, "channel.base_latency_ticks", "\"channel\": {\"base_latency\": -1, \"per_distance\": 0.0, \"loss\": 0.1}", "channel.base_latency_ticks must be >= 0, got -1");
    check_invalid_field(ctx, "channel.latency_per_distance", "\"channel\": {\"base_latency\": 3, \"per_distance\": -0.5, \"loss\": 0.1}", "channel.latency_per_distance must be >= 0, got -0.5");
    check_invalid_field(ctx, "channel.loss_probability", "\"channel\": {\"base_latency\": 3, \"per_distance\": 0.0, \"loss\": 1.5}", "channel.loss_probability must be in [0, 1], got 1.5");
    check_invalid_field(ctx, "belief.fresh_ticks", "\"belief\": {\"fresh_ticks\": -1, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": 0.05}", "belief.fresh_ticks must be >= 0, got -1");
    check_invalid_field(ctx, "belief.stale_ticks", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": -1, \"uncertainty_growth\": 0.5, \"confidence_decay\": 0.05}", "belief.stale_ticks must be >= 0, got -1");
    check_invalid_field(ctx, "belief.uncertainty_growth_per_second", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": -0.1, \"confidence_decay\": 0.05}", "belief.uncertainty_growth_per_second must be >= 0, got -0.1");
    check_invalid_field(ctx, "belief.confidence_decay_per_second", "\"belief\": {\"fresh_ticks\": 5, \"stale_ticks\": 10, \"uncertainty_growth\": 0.5, \"confidence_decay\": -0.05}", "belief.confidence_decay_per_second must be >= 0, got -0.05");
}

static void test_invalid_effect_profile_reference(TestContext& ctx) {
    const char* path = "tests/tmp_invalid_effect_profile_ref.json";
    write_temp_scenario(path,
        "{"
        "\"seed\":1,"
        "\"obstacles\":[],"
        "\"effect_profiles\":["
        "{\"name\":\"std\",\"range\":20,\"requires_los\":true,\"identity_threshold\":0.6,\"corroboration_threshold\":0.5,\"cooldown_ticks\":1,\"ammo_cost\":1}"
        "],"
        "\"entities\":["
        "{\"id\":0,\"type\":\"drone\",\"pos\":[0,0],\"vel\":[0,0],\"can_sense\":true,\"can_engage\":true,\"allowed_effect_profile_indices\":[9]},"
        "{\"id\":1,\"type\":\"ground\",\"pos\":[1,0],\"vel\":[0,0],\"can_track\":true},"
        "{\"id\":2,\"type\":\"target\",\"pos\":[5,5],\"vel\":[0,0],\"is_observable\":true}"
        "]"
        "}");

    bool caught = false;
    std::string message;
    try {
        load_scenario(path);
    } catch (const std::runtime_error& e) {
        caught = true;
        message = e.what();
    }
    ctx.check(caught, "invalid effect profile reference rejected");
    ctx.check(message.find(path) != std::string::npos, "invalid profile ref includes path");
    ctx.check(message.find("allowed_effect_profile_indices[0]") != std::string::npos,
          "invalid profile ref includes offending field");
    std::remove(path);
}

int main() {
    TestContext ctx;
    std::printf("Running scenario tests...\n");
    test_load_default(ctx);
    test_load_los_blocked(ctx);
    test_missing_file(ctx);
    test_multi_drone_accepted(ctx);
    test_multi_ground_accepted(ctx);
    test_custom_role_accepted(ctx);
    test_capability_override(ctx);
    test_missing_capabilities_rejected(ctx);
    test_duplicate_entity_ids_rejected(ctx);
    test_belief_rate_units_per_second_keys(ctx);
    test_invalid_validations(ctx);
    test_invalid_effect_profile_reference(ctx);
    return ctx.report_and_exit_code();
}
