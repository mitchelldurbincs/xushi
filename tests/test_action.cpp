#include "test_helpers.h"
#include "../src/sim_engine.h"
#include "../src/action.h"

// Minimal scenario with one sensor/tracker and one observable target.
static Scenario make_minimal_scenario() {
    Scenario s;
    s.seed = 42;
    s.dt = 1.0f;
    s.ticks = 100;
    s.max_sensor_range = 80.0f;

    ScenarioEntity sensor;
    sensor.id = 0;
    sensor.role_name = "drone";
    sensor.position = {10, 10};
    sensor.velocity = {0, 0};
    sensor.can_sense = true;
    sensor.can_track = true;
    sensor.is_observable = false;

    ScenarioEntity target;
    target.id = 1;
    target.role_name = "target";
    target.position = {20, 20};
    target.velocity = {0, 0};
    target.can_sense = false;
    target.can_track = false;
    target.is_observable = true;

    s.entities = {sensor, target};
    return s;
}

// Hook that records action results for verification.
struct RecordingHooks : TickHooks {
    std::vector<ActionResult> results;
    void on_action_resolved(int /*tick*/, const ActionResult& r) override {
        results.push_back(r);
    }
};

static void test_designate_accepted_when_track_exists(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Run a few ticks so sensor detects target and creates a track
    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    // Verify we have a track for entity 1
    auto& beliefs = engine.get_beliefs();
    auto it = beliefs.find(0);
    ctx.check(it != beliefs.end(), "sensor has belief state");
    if (it == beliefs.end()) return;
    const Track* trk = it->second.find_track(1);
    ctx.check(trk != nullptr, "track exists for target");

    // Submit designation
    hooks.results.clear();
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::DesignateTrack;
    req.track_target = 1;
    req.desig_kind = DesignationKind::Engage;
    req.priority = 5;
    engine.submit_action(req);
    engine.step(5, hooks);

    ctx.check(hooks.results.size() == 1, "one action result");
    ctx.check(hooks.results[0].allowed, "designation accepted");
    ctx.check(hooks.results[0].failure_mask == 0, "no failure reasons");

    auto& desigs = engine.get_designations();
    ctx.check(desigs.size() == 1, "one designation stored");
    ctx.check(desigs[0].track_target == 1, "designation targets entity 1");
    ctx.check(desigs[0].kind == DesignationKind::Engage, "designation kind matches");
    ctx.check(desigs[0].priority == 5, "priority matches");
    ctx.check(desigs[0].issuer == 0, "issuer matches");
}

static void test_designate_rejected_track_not_found(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Run one tick so belief state exists but don't expect track for entity 99
    engine.step(0, hooks);

    hooks.results.clear();
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::DesignateTrack;
    req.track_target = 99;  // no such track
    req.desig_kind = DesignationKind::Observe;
    engine.submit_action(req);
    engine.step(1, hooks);

    ctx.check(hooks.results.size() == 1, "one action result");
    ctx.check(!hooks.results[0].allowed, "designation rejected");
    ctx.check(has_reason(hooks.results[0].failure_mask, GateFailureReason::TrackNotFound),
          "failure reason is TrackNotFound");
}

static void test_engage_rejected_no_capability(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Get a track first
    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    hooks.results.clear();
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::EngageTrack;
    req.track_target = 1;
    engine.submit_action(req);
    engine.step(5, hooks);

    ctx.check(hooks.results.size() == 1, "one action result");
    ctx.check(!hooks.results[0].allowed, "engage rejected");
    ctx.check(has_reason(hooks.results[0].failure_mask, GateFailureReason::NoCapability),
          "failure reason is NoCapability");
}

static void test_bda_rejected_no_capability(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    for (int t = 0; t < 3; ++t)
        engine.step(t, hooks);

    hooks.results.clear();
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::RequestBDA;
    req.track_target = 1;
    engine.submit_action(req);
    engine.step(3, hooks);

    ctx.check(hooks.results.size() == 1, "one action result");
    ctx.check(!hooks.results[0].allowed, "BDA rejected");
    ctx.check(has_reason(hooks.results[0].failure_mask, GateFailureReason::NoCapability),
          "failure reason is NoCapability");
}

static void test_designation_expires_after_ttl(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Build a track
    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    // Designate at tick 5
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::DesignateTrack;
    req.track_target = 1;
    req.desig_kind = DesignationKind::Observe;
    engine.submit_action(req);
    engine.step(5, hooks);

    ctx.check(engine.get_designations().size() == 1, "designation exists");

    // Advance past TTL (default 30 ticks, so tick 35 should expire it)
    for (int t = 6; t <= 35; ++t)
        engine.step(t, hooks);

    ctx.check(engine.get_designations().empty(), "designation expired after TTL");
}

static void test_clear_designation_removes_record(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Build a track and designate
    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    ActionRequest desig;
    desig.actor = 0;
    desig.type = ActionType::DesignateTrack;
    desig.track_target = 1;
    desig.desig_kind = DesignationKind::Verify;
    engine.submit_action(desig);
    engine.step(5, hooks);
    ctx.check(engine.get_designations().size() == 1, "designation created");

    // Clear it
    hooks.results.clear();
    ActionRequest clear;
    clear.actor = 0;
    clear.type = ActionType::ClearDesignation;
    clear.track_target = 1;
    clear.desig_kind = DesignationKind::Verify;
    engine.submit_action(clear);
    engine.step(6, hooks);

    ctx.check(hooks.results.size() == 1, "one result for clear");
    ctx.check(hooks.results[0].allowed, "clear accepted");
    ctx.check(engine.get_designations().empty(), "designation removed");
}

static void test_clear_nonexistent_designation_rejected(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;
    engine.step(0, hooks);

    hooks.results.clear();
    ActionRequest clear;
    clear.actor = 0;
    clear.type = ActionType::ClearDesignation;
    clear.track_target = 99;
    clear.desig_kind = DesignationKind::Observe;
    engine.submit_action(clear);
    engine.step(1, hooks);

    ctx.check(!hooks.results[0].allowed, "clear of nonexistent rejected");
    ctx.check(has_reason(hooks.results[0].failure_mask, GateFailureReason::TrackNotFound),
          "reason is TrackNotFound");
}

static void test_multiple_actions_per_tick(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    // Build tracks
    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    hooks.results.clear();

    // Submit three actions in one tick
    ActionRequest r1;
    r1.actor = 0;
    r1.type = ActionType::DesignateTrack;
    r1.track_target = 1;
    r1.desig_kind = DesignationKind::Observe;
    engine.submit_action(r1);

    ActionRequest r2;
    r2.actor = 0;
    r2.type = ActionType::EngageTrack;
    r2.track_target = 1;
    engine.submit_action(r2);

    ActionRequest r3;
    r3.actor = 0;
    r3.type = ActionType::RequestBDA;
    r3.track_target = 1;
    engine.submit_action(r3);

    engine.step(5, hooks);

    ctx.check(hooks.results.size() == 3, "three action results");
    ctx.check(hooks.results[0].allowed, "designate accepted");
    ctx.check(!hooks.results[1].allowed, "engage rejected");
    ctx.check(!hooks.results[2].allowed, "BDA rejected");
}

static void test_action_result_echoes_request(TestContext& ctx) {
    Scenario scn = make_minimal_scenario();
    SimEngine engine;
    engine.init(scn);

    RecordingHooks hooks;

    for (int t = 0; t < 3; ++t)
        engine.step(t, hooks);

    hooks.results.clear();
    ActionRequest req;
    req.actor = 0;
    req.type = ActionType::EngageTrack;
    req.track_target = 1;
    req.effect_profile_index = 7;
    engine.submit_action(req);
    engine.step(3, hooks);

    ctx.check(hooks.results[0].request.actor == 0, "echoed actor");
    ctx.check(hooks.results[0].request.type == ActionType::EngageTrack, "echoed type");
    ctx.check(hooks.results[0].request.track_target == 1, "echoed track_target");
    ctx.check(hooks.results[0].request.effect_profile_index == 7, "echoed effect_profile_index");
    ctx.check(hooks.results[0].tick == 3, "result tick matches");
}

int main() {
    TestContext ctx;
    std::printf("Running action tests...\n");
    test_designate_accepted_when_track_exists(ctx);
    test_designate_rejected_track_not_found(ctx);
    test_engage_rejected_no_capability(ctx);
    test_bda_rejected_no_capability(ctx);
    test_designation_expires_after_ttl(ctx);
    test_clear_designation_removes_record(ctx);
    test_clear_nonexistent_designation_rejected(ctx);
    test_multiple_actions_per_tick(ctx);
    test_action_result_echoes_request(ctx);
    return ctx.report_and_exit_code();
}
