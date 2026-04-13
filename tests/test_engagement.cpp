#include "test_helpers.h"
#include "../src/engagement.h"

static void test_engagement_gates_include_track_and_truth_failures() {
    ScenarioEntity actor;
    actor.id = 7;
    actor.position = {0.0f, 0.0f};

    ScenarioEntity target;
    target.id = 9;
    target.position = {200.0f, 0.0f};

    Track trk;
    trk.target = 9;
    trk.status = TrackStatus::STALE;
    trk.last_update_tick = 3;
    trk.uncertainty = 30.0f;
    trk.identity_confidence = 0.2f;
    trk.corroboration_count = 0;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.world.tick = 10;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(!out.allowed(), "engagement rejected");
    CHECK(has_reason(out.failure_mask, GateFailureReason::NoCapability), "includes capability gate");
    CHECK(has_reason(out.failure_mask, GateFailureReason::TrackTooStale), "flags stale tracks");
    CHECK(has_reason(out.failure_mask, GateFailureReason::TrackTooUncertain), "flags uncertainty");
    CHECK(has_reason(out.failure_mask, GateFailureReason::IdentityTooWeak), "flags identity");
    CHECK(has_reason(out.failure_mask, GateFailureReason::NeedsCorroboration), "flags corroboration");
    CHECK(has_reason(out.failure_mask, GateFailureReason::OutOfRange), "flags range");

    CHECK(out.debug.track_age_ticks.has_value(), "debug has age");
    CHECK(out.debug.track_age_ticks.value() == 7, "age derived from world tick");
    CHECK(out.debug.actor_to_truth_distance.has_value(), "debug has distance");
}

int main() {
    std::printf("Running engagement tests...\n");
    test_engagement_gates_include_track_and_truth_failures();
    TEST_REPORT();
}
