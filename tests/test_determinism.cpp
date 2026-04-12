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

int main() {
    std::printf("Running determinism tests...\n");
    test_same_seed_same_hashes();
    test_same_seed_same_counters();
    test_different_seed_different_hashes();
    test_benchmark_determinism();
    test_multi_agent_determinism();
    TEST_REPORT();
}
