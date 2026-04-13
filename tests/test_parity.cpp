#include "test_helpers.h"
#include "../src/sim.h"
#include "../src/sim_engine.h"
#include "../src/scenario.h"

// Hooks that collect world hashes and stats for comparison with headless path.
struct HashCollectHooks : TickHooks {
    std::vector<uint64_t> world_hashes;
    void on_world_hash(int /*tick*/, uint64_t hash) override {
        world_hashes.push_back(hash);
    }
};

static void test_parity_default(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/default.json");

    // Headless path (uses SimEngine internally)
    SimResult headless = run_scenario_headless(scn);

    // Direct engine path with hooks
    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(!headless.world_hashes.empty(), "parity: headless produced hashes");
    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: same hash count");

    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: default world hashes match");
    ctx.check(headless.stats.detections_generated == engine.stats().detections_generated,
          "parity: default detection count");
    ctx.check(headless.stats.messages_sent == engine.stats().messages_sent,
          "parity: default messages sent");
    ctx.check(headless.stats.messages_delivered == engine.stats().messages_delivered,
          "parity: default messages delivered");
    ctx.check(headless.tasks_assigned == engine.tasks_assigned(),
          "parity: default tasks assigned");
    ctx.check(headless.tasks_completed == engine.tasks_completed(),
          "parity: default tasks completed");
}

static void test_parity_benchmark_dense(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/benchmark_dense.json");

    SimResult headless = run_scenario_headless(scn);

    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: dense hash count");
    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: dense world hashes match");
    ctx.check(headless.stats.detections_generated == engine.stats().detections_generated,
          "parity: dense detection count");
}

static void test_parity_noisy(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/noisy_perception.json");

    SimResult headless = run_scenario_headless(scn);

    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: noisy hash count");
    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: noisy world hashes match");
}

static void test_parity_task_verify(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/task_verify.json");

    SimResult headless = run_scenario_headless(scn);

    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: task hash count");
    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: task world hashes match");
    ctx.check(headless.tasks_assigned == engine.tasks_assigned(),
          "parity: task assignments match");
    ctx.check(headless.tasks_completed == engine.tasks_completed(),
          "parity: task completions match");
}

static void test_parity_waypoint(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/waypoint_patrol.json");

    SimResult headless = run_scenario_headless(scn);

    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: waypoint hash count");
    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: waypoint world hashes match");
}

static void test_parity_multi_agent(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/multi_agent.json");

    SimResult headless = run_scenario_headless(scn);

    SimEngine engine;
    engine.init(scn);
    HashCollectHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    ctx.check(headless.world_hashes.size() == hooks.world_hashes.size(), "parity: multi-agent hash count");
    bool all_match = true;
    for (size_t i = 0; i < headless.world_hashes.size(); ++i) {
        if (headless.world_hashes[i] != hooks.world_hashes[i]) {
            all_match = false;
            break;
        }
    }
    ctx.check(all_match, "parity: multi-agent world hashes match");
    ctx.check(headless.stats.detections_generated == engine.stats().detections_generated,
          "parity: multi-agent detection count");
    ctx.check(headless.stats.messages_sent == engine.stats().messages_sent,
          "parity: multi-agent messages sent");
}

int main() {
    TestContext ctx;
    std::printf("Running parity tests...\n");
    test_parity_default(ctx);
    test_parity_benchmark_dense(ctx);
    test_parity_noisy(ctx);
    test_parity_task_verify(ctx);
    test_parity_waypoint(ctx);
    test_parity_multi_agent(ctx);
    return ctx.report_and_exit_code();
}
