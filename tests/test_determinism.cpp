#include "test_helpers.h"
#include "../src/sim.h"
#include "../src/sim_engine.h"
#include "../src/scenario.h"

static void test_same_seed_same_hashes(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/default.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "same hash count");

    bool all_match = true;
    for (size_t i = 0; i < a.world_hashes.size(); ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "all world hashes match across two runs");
}

static void test_same_seed_same_counters(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/default.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(a.stats.detections_generated == b.stats.detections_generated, "same detection count");
    ctx.check(a.stats.messages_sent == b.stats.messages_sent, "same messages sent");
    ctx.check(a.stats.messages_dropped == b.stats.messages_dropped, "same messages dropped");
    ctx.check(a.stats.messages_delivered == b.stats.messages_delivered, "same messages delivered");
    ctx.check(a.stats.tracks_expired == b.stats.tracks_expired, "same tracks expired");
    ctx.check(a.final_track_count == b.final_track_count, "same final track count");
}

static void test_different_seed_different_hashes(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/default.json");

    SimResult a = run_scenario_headless(scn);
    scn.seed = scn.seed + 1;
    SimResult b = run_scenario_headless(scn);

    bool any_differ = false;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            any_differ = true;
            break;
        }
    }
    ctx.check(any_differ, "different seed produces different hashes");
}

static void test_benchmark_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/benchmark_dense.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "benchmark hash count matches");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "benchmark scenario deterministic");
}

static void test_noisy_perception_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/noisy_perception.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "noisy perception hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "noisy perception hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "noisy perception deterministic");
}

static void test_task_verify_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/task_verify.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "task verify hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "task verify hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "task verify deterministic");
    ctx.check(a.tasks_assigned > 0, "task verify assigned tasks");
    ctx.check(a.tasks_assigned == b.tasks_assigned, "task verify same assignments");
    ctx.check(a.tasks_completed == b.tasks_completed, "task verify same completions");
}

static void test_mixed_era_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/mixed_era.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "mixed-era hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "mixed-era hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "mixed-era deterministic");
}

static void test_waypoint_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/waypoint_patrol.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "waypoint hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "waypoint hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "waypoint scenario deterministic");
}

static void test_multi_agent_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/multi_agent.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "multi-agent hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "multi-agent hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "multi-agent deterministic");

    ctx.check(a.stats.detections_generated == b.stats.detections_generated, "multi-agent same detections");
    ctx.check(a.stats.messages_sent == b.stats.messages_sent, "multi-agent same messages sent");
    ctx.check(a.final_track_count == b.final_track_count, "multi-agent same final tracks");
}

static void test_branch_waypoint_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/branch_waypoint.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "branch waypoint hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "branch waypoint hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "branch waypoint deterministic");
}

static void test_distance_comms_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/distance_comms.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "distance comms hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "distance comms hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "distance comms deterministic");
    ctx.check(a.stats.messages_delivered == b.stats.messages_delivered, "distance comms same deliveries");
}

static void test_patrol_policy_determinism(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/patrol_policy.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    ctx.check(!a.world_hashes.empty(), "patrol policy hashes produced");
    ctx.check(a.world_hashes.size() == b.world_hashes.size(), "patrol policy hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "patrol policy deterministic");
    ctx.check(a.stats.detections_generated == b.stats.detections_generated, "patrol policy same detections");
    ctx.check(a.stats.detections_generated > 0, "patrol policy produces detections");
}

static void test_hash_api_consistency(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/noisy_perception.json");

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick) {
        engine.step(tick, hooks);
        uint64_t engine_hash = engine.compute_world_hash();
        uint64_t free_hash = compute_world_hash(engine.get_entities(), engine.get_beliefs());
        ctx.check(engine_hash == free_hash, "hash api consistency per tick");
    }
}

int main() {
    TestContext ctx;
    std::printf("Running determinism tests...\n");
    test_same_seed_same_hashes(ctx);
    test_same_seed_same_counters(ctx);
    test_different_seed_different_hashes(ctx);
    test_benchmark_determinism(ctx);
    test_multi_agent_determinism(ctx);
    test_waypoint_determinism(ctx);
    test_mixed_era_determinism(ctx);
    test_task_verify_determinism(ctx);
    test_noisy_perception_determinism(ctx);
    test_branch_waypoint_determinism(ctx);
    test_distance_comms_determinism(ctx);
    test_patrol_policy_determinism(ctx);
    test_hash_api_consistency(ctx);
    return ctx.report_and_exit_code();
}
