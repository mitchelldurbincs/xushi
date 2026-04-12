#include "test_helpers.h"
#include "../src/sim.h"
#include "../src/scenario.h"

static void test_same_seed_same_hashes() {
    Scenario scn = load_scenario("scenarios/default.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "same hash count");

    bool all_match = true;
    for (size_t i = 0; i < a.world_hashes.size(); ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "all world hashes match across two runs");
}

static void test_same_seed_same_counters() {
    Scenario scn = load_scenario("scenarios/default.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(a.stats.detections_generated == b.stats.detections_generated, "same detection count");
    CHECK(a.stats.messages_sent == b.stats.messages_sent, "same messages sent");
    CHECK(a.stats.messages_dropped == b.stats.messages_dropped, "same messages dropped");
    CHECK(a.stats.messages_delivered == b.stats.messages_delivered, "same messages delivered");
    CHECK(a.stats.tracks_expired == b.stats.tracks_expired, "same tracks expired");
    CHECK(a.final_track_count == b.final_track_count, "same final track count");
}

static void test_different_seed_different_hashes() {
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
    CHECK(any_differ, "different seed produces different hashes");
}

static void test_benchmark_determinism() {
    Scenario scn = load_scenario("scenarios/benchmark_dense.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(a.world_hashes.size() == b.world_hashes.size(), "benchmark hash count matches");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "benchmark scenario deterministic");
}

static void test_noisy_perception_determinism() {
    Scenario scn = load_scenario("scenarios/noisy_perception.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "noisy perception hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "noisy perception hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "noisy perception deterministic");
}

static void test_task_verify_determinism() {
    Scenario scn = load_scenario("scenarios/task_verify.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "task verify hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "task verify hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "task verify deterministic");
    CHECK(a.tasks_assigned > 0, "task verify assigned tasks");
    CHECK(a.tasks_assigned == b.tasks_assigned, "task verify same assignments");
    CHECK(a.tasks_completed == b.tasks_completed, "task verify same completions");
}

static void test_mixed_era_determinism() {
    Scenario scn = load_scenario("scenarios/mixed_era.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "mixed-era hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "mixed-era hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "mixed-era deterministic");
}

static void test_waypoint_determinism() {
    Scenario scn = load_scenario("scenarios/waypoint_patrol.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "waypoint hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "waypoint hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "waypoint scenario deterministic");
}

static void test_multi_agent_determinism() {
    Scenario scn = load_scenario("scenarios/multi_agent.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "multi-agent hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "multi-agent hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "multi-agent deterministic");

    CHECK(a.stats.detections_generated == b.stats.detections_generated, "multi-agent same detections");
    CHECK(a.stats.messages_sent == b.stats.messages_sent, "multi-agent same messages sent");
    CHECK(a.final_track_count == b.final_track_count, "multi-agent same final tracks");
}

static void test_branch_waypoint_determinism() {
    Scenario scn = load_scenario("scenarios/branch_waypoint.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "branch waypoint hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "branch waypoint hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "branch waypoint deterministic");
}

static void test_distance_comms_determinism() {
    Scenario scn = load_scenario("scenarios/distance_comms.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "distance comms hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "distance comms hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "distance comms deterministic");
    CHECK(a.stats.messages_delivered == b.stats.messages_delivered, "distance comms same deliveries");
}

static void test_patrol_policy_determinism() {
    Scenario scn = load_scenario("scenarios/patrol_policy.json");

    SimResult a = run_scenario_headless(scn);
    SimResult b = run_scenario_headless(scn);

    CHECK(!a.world_hashes.empty(), "patrol policy hashes produced");
    CHECK(a.world_hashes.size() == b.world_hashes.size(), "patrol policy hash count");

    bool all_match = true;
    size_t count = std::min(a.world_hashes.size(), b.world_hashes.size());
    for (size_t i = 0; i < count; ++i) {
        if (a.world_hashes[i] != b.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    CHECK(all_match, "patrol policy deterministic");
    CHECK(a.stats.detections_generated == b.stats.detections_generated, "patrol policy same detections");
    CHECK(a.stats.detections_generated > 0, "patrol policy produces detections");
}

int main() {
    std::printf("Running determinism tests...\n");
    test_same_seed_same_hashes();
    test_same_seed_same_counters();
    test_different_seed_different_hashes();
    test_benchmark_determinism();
    test_multi_agent_determinism();
    test_waypoint_determinism();
    test_mixed_era_determinism();
    test_task_verify_determinism();
    test_noisy_perception_determinism();
    test_branch_waypoint_determinism();
    test_distance_comms_determinism();
    test_patrol_policy_determinism();
    TEST_REPORT();
}
