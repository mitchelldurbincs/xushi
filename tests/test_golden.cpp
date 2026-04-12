#include "test_helpers.h"
#include "../src/map.h"
#include "../src/sensing.h"
#include "../src/comm.h"
#include "../src/belief.h"
#include "../src/rng.h"

// Golden tests from the simulation contract (section 14.3).
// Each test builds a tiny scenario in code and asserts expected outcomes.

static void test_los_blocked_no_detection() {
    // Obstacle directly between drone and target.
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});

    Rng rng(1);
    int detections = 0;

    for (int tick = 0; tick < 20; ++tick) {
        Observation obs{};
        // Drone at (0,5), target at (10,5), obstacle at (4..6, 4..6) blocks LOS.
        if (sense(map, {0, 5}, 0, {10, 5}, 1, 100.0f, tick, rng, obs))
            detections++;
    }

    CHECK(detections == 0, "LOS blocked => no detection over 20 ticks");
}

static void test_clear_los_detection_on_tick_0() {
    // No obstacles, target within range.
    Map map;
    Rng rng(42);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs);
    CHECK(detected, "clear LOS => detection on tick 0");
    CHECK(obs.tick == 0, "observation has correct tick");
}

static void test_delayed_comms_belief_lags() {
    // Drone detects on tick 0, channel latency = 5.
    // Ground should have no track until tick 5.
    Map map;
    Rng rng(100);
    CommChannel ch = {5, 0.0f, 0.0f}; // 5 tick delay, no loss
    CommSystem comms;
    BeliefState belief;
    BeliefConfig cfg;

    // Drone detects on tick 0
    Observation obs{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 100.0f, 0, rng, obs);

    MessagePayload payload;
    payload.type = MessagePayload::OBSERVATION;
    payload.observation = obs;
    comms.send(0, 1, payload, 0, 0.0f, ch, rng);

    // Ticks 0-4: ground has no track
    bool no_track_before = true;
    for (int tick = 0; tick < 5; ++tick) {
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        for (const auto& msg : delivered)
            belief.update(msg.payload.observation, tick);
        belief.decay(tick, cfg);
        if (belief.find_track(1) != nullptr)
            no_track_before = false;
    }
    CHECK(no_track_before, "delayed comms => no track before delivery tick");

    // Tick 5: message arrives, ground gets track
    std::vector<Message> delivered;
    comms.deliver(5, delivered);
    CHECK(delivered.size() == 1, "message delivered on tick 5");
    for (const auto& msg : delivered)
        belief.update(msg.payload.observation, 5);
    belief.decay(5, cfg);
    CHECK(belief.find_track(1) != nullptr, "delayed comms => track appears at delivery tick");
}

static void test_stale_track_expires_by_tick_n() {
    // Single observation at tick 0, then silence.
    // With fresh_ticks=3, stale_ticks=5, track should expire at tick 9.
    BeliefConfig cfg;
    cfg.fresh_ticks = 3;
    cfg.stale_ticks = 5;

    BeliefState belief;
    Observation obs = {0, 0, 1, {10, 20}, 1.0f, 0.9f};
    belief.update(obs, 0);

    // Track should exist through tick 8
    for (int tick = 1; tick <= 8; ++tick)
        belief.decay(tick, cfg);

    CHECK(belief.find_track(1) != nullptr, "track alive at tick 8 (fresh=3 + stale=5)");

    // Track should expire at tick 9
    belief.decay(9, cfg);
    CHECK(belief.find_track(1) == nullptr, "stale track expired by tick 9");
}

int main() {
    std::printf("Running golden tests...\n");
    test_los_blocked_no_detection();
    test_clear_los_detection_on_tick_0();
    test_delayed_comms_belief_lags();
    test_stale_track_expires_by_tick_n();
    TEST_REPORT();
}
