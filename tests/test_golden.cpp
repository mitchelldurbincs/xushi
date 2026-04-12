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
        belief.decay(tick, 1.0f, cfg);
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
    belief.decay(5, 1.0f, cfg);
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
        belief.decay(tick, 1.0f, cfg);

    CHECK(belief.find_track(1) != nullptr, "track alive at tick 8 (fresh=3 + stale=5)");

    // Track should expire at tick 9
    belief.decay(9, 1.0f, cfg);
    CHECK(belief.find_track(1) == nullptr, "stale track expired by tick 9");
}

static void test_distance_comms_latency_calculation() {
    // Two agents: near (10m from target) and far (200m from target).
    // Channel: base_latency=2, per_distance=0.05, loss=0.
    // Near agent self-observes (distance 0 to self for comms), but the
    // interesting case is cross-agent delivery.
    // Distance between near sensor (10,50) and far sensor (210,50) = 200m.
    // Expected latency: base(2) + ceil(200 * 0.05) = 2 + 10 = 12 ticks.
    Rng rng(99);
    CommChannel ch = {2, 0.05f, 0.0f};
    CommSystem comms;

    // Simulate near sensor sending to far sensor
    float distance = 200.0f;
    MessagePayload payload;
    payload.type = MessagePayload::OBSERVATION;
    payload.observation = {0, 0, 2, {20, 50}, 1.0f, 0.9f};

    int delivery = comms.send(0, 1, payload, 0, distance, ch, rng);
    CHECK(delivery == 12, "distance comms: 200m delivery at tick 12 (base 2 + ceil(200*0.05)=10)");

    // Simulate near sensor sending to itself (distance 0)
    int self_delivery = comms.send(0, 0, payload, 0, 0.0f, ch, rng);
    CHECK(self_delivery == 2, "distance comms: 0m delivery at tick 2 (base only)");
}

static void test_distance_comms_far_belief_lags_near() {
    // Near sensor detects target on tick 0.
    // Near sensor (also tracker) gets immediate self-update.
    // Far sensor gets message after distance-dependent delay.
    Map map;
    Rng rng(99);
    CommChannel ch = {2, 0.05f, 0.0f};
    CommSystem comms;
    BeliefConfig cfg;

    BeliefState near_belief, far_belief;

    // Near sensor detects target
    Observation obs{};
    sense(map, {10, 50}, 0, {20, 50}, 2, 250.0f, 0, rng, obs);

    // Near sensor updates own belief immediately
    near_belief.update(obs, 0);
    CHECK(near_belief.find_track(2) != nullptr, "distance comms: near has track at tick 0");

    // Send to far sensor (200m away)
    MessagePayload payload;
    payload.type = MessagePayload::OBSERVATION;
    payload.observation = obs;
    int delivery_tick = comms.send(0, 1, payload, 0, 200.0f, ch, rng);

    // Far sensor should NOT have track before delivery
    bool no_track_before = true;
    for (int tick = 0; tick < delivery_tick; ++tick) {
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        for (const auto& msg : delivered)
            far_belief.update(msg.payload.observation, tick);
        far_belief.decay(tick, 1.0f, cfg);
        if (far_belief.find_track(2) != nullptr)
            no_track_before = false;
    }
    CHECK(no_track_before, "distance comms: far has no track before delivery");

    // At delivery tick, far sensor gets the track
    std::vector<Message> delivered;
    comms.deliver(delivery_tick, delivered);
    CHECK(delivered.size() == 1, "distance comms: message delivered at expected tick");
    for (const auto& msg : delivered)
        far_belief.update(msg.payload.observation, delivery_tick);
    CHECK(far_belief.find_track(2) != nullptr, "distance comms: far has track at delivery tick");
}

int main() {
    std::printf("Running golden tests...\n");
    test_los_blocked_no_detection();
    test_clear_los_detection_on_tick_0();
    test_delayed_comms_belief_lags();
    test_stale_track_expires_by_tick_n();
    test_distance_comms_latency_calculation();
    test_distance_comms_far_belief_lags_near();
    TEST_REPORT();
}
