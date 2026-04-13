#include "test_helpers.h"
#include "../src/belief.h"
#include <cmath>

static Observation make_obs(EntityId target, Vec2 pos, int tick,
                            EntityId observer = 0) {
    Observation obs{};
    obs.tick = tick;
    obs.observer = observer;
    obs.target = target;
    obs.estimated_position = pos;
    obs.uncertainty = 1.0f;
    obs.confidence = 0.8f;
    obs.class_id = 0;
    obs.identity_confidence = 0.0f;
    return obs;
}

static Observation make_obs_with_identity(EntityId target, Vec2 pos, int tick,
                                          EntityId observer, int class_id,
                                          float id_conf) {
    Observation obs = make_obs(target, pos, tick, observer);
    obs.class_id = class_id;
    obs.identity_confidence = id_conf;
    return obs;
}

static void test_new_observation_creates_fresh_track(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    ctx.check(bs.tracks.size() == 1, "one track created");
    ctx.check(bs.tracks[0].target == 1, "correct target id");
    ctx.check(bs.tracks[0].status == TrackStatus::FRESH, "status is FRESH");
}

static void test_duplicate_observation_updates_not_duplicates(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.update(make_obs(1, {15, 25}, 5), 5);
    ctx.check(bs.tracks.size() == 1, "still one track after second obs");
    ctx.check(bs.tracks[0].estimated_position.x == 15.0f, "position updated");
    ctx.check(bs.tracks[0].last_update_tick == 5, "tick updated");
}

static void test_fresh_to_stale_transition(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.decay(5, 1.0f, cfg);
    ctx.check(bs.tracks[0].status == TrackStatus::FRESH, "still fresh at boundary");
    bs.decay(6, 1.0f, cfg);
    ctx.check(bs.tracks[0].status == TrackStatus::STALE, "stale after fresh_ticks");
}

static void test_stale_to_expired_removal(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.stale_ticks = 10;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.decay(15, 1.0f, cfg);
    ctx.check(bs.tracks.size() == 1, "still alive at stale boundary");
    bs.decay(16, 1.0f, cfg);
    ctx.check(bs.tracks.empty(), "expired and removed");
}

static void test_uncertainty_grows_when_stale(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.uncertainty_growth_per_second = 0.5f;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    float initial_unc = bs.tracks[0].uncertainty;
    bs.decay(6, 1.0f, cfg);
    ctx.check(bs.tracks[0].uncertainty > initial_unc, "uncertainty grew");
}

static void test_confidence_decays_when_stale(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.confidence_decay_per_second = 0.05f;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    float initial_conf = bs.tracks[0].confidence;
    bs.decay(6, 1.0f, cfg);
    ctx.check(bs.tracks[0].confidence < initial_conf, "confidence decayed");
}

static void test_fresh_observation_resets_stale_track(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.decay(8, 1.0f, cfg);
    ctx.check(bs.tracks[0].status == TrackStatus::STALE, "stale before refresh");
    bs.update(make_obs(1, {30, 40}, 8), 8);
    ctx.check(bs.tracks[0].status == TrackStatus::FRESH, "back to fresh after update");
    ctx.check(bs.tracks[0].estimated_position.x == 30.0f, "position refreshed");
}

static void test_multiple_independent_targets(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.stale_ticks = 10;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.update(make_obs(2, {30, 40}, 0), 0);
    ctx.check(bs.tracks.size() == 2, "two tracks for two targets");
    bs.update(make_obs(1, {15, 25}, 10), 10);
    bs.decay(16, 1.0f, cfg);
    ctx.check(bs.tracks.size() == 1, "expired target removed, other survives");
    ctx.check(bs.tracks[0].target == 1, "surviving track is target 1");
}

static void test_confidence_floors_at_zero(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 0;
    cfg.stale_ticks = 100;
    cfg.confidence_decay_per_second = 1.0f;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.decay(5, 1.0f, cfg);
    ctx.check(bs.tracks[0].confidence >= 0.0f, "confidence doesn't go negative");
}

static void test_decay_equivalent_across_dt_for_equal_sim_time(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 0;
    cfg.stale_ticks = 1000;
    cfg.uncertainty_growth_per_second = 0.4f;
    cfg.confidence_decay_per_second = 0.2f;

    BeliefState coarse_dt;
    coarse_dt.update(make_obs(1, {10, 20}, 0), 0);
    for (int tick = 1; tick <= 10; ++tick)
        coarse_dt.decay(tick, 1.0f, cfg); // 10 seconds total

    BeliefState fine_dt;
    fine_dt.update(make_obs(1, {10, 20}, 0), 0);
    for (int tick = 1; tick <= 20; ++tick)
        fine_dt.decay(tick, 0.5f, cfg); // 10 seconds total

    ctx.check(std::fabs(coarse_dt.tracks[0].uncertainty - fine_dt.tracks[0].uncertainty) < 1e-5f,
          "uncertainty matches across dt for equal sim time");
    ctx.check(std::fabs(coarse_dt.tracks[0].confidence - fine_dt.tracks[0].confidence) < 1e-5f,
          "confidence matches across dt for equal sim time");
}

static void test_new_track_carries_class_and_identity(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, /*observer=*/0, /*class_id=*/3, /*id_conf=*/0.7f), 0);
    ctx.check(bs.tracks.size() == 1, "track created");
    ctx.check(bs.tracks[0].class_id == 3, "class_id carried from observation");
    ctx.check(std::fabs(bs.tracks[0].identity_confidence - 0.7f) < 1e-6f, "identity_confidence carried");
    ctx.check(bs.tracks[0].corroboration_count == 1, "single source counted");
}

static void test_corroboration_increments_on_new_source(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, /*observer=*/0, 3, 0.5f), 0);
    ctx.check(bs.tracks[0].corroboration_count == 1, "one source");
    bs.update(make_obs_with_identity(1, {11, 21}, 1, /*observer=*/1, 3, 0.6f), 1);
    ctx.check(bs.tracks[0].corroboration_count == 2, "two sources after different observer");
    bs.update(make_obs_with_identity(1, {12, 22}, 2, /*observer=*/2, 3, 0.4f), 2);
    ctx.check(bs.tracks[0].corroboration_count == 3, "three sources");
}

static void test_corroboration_no_double_count_same_source(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, /*observer=*/5, 3, 0.5f), 0);
    bs.update(make_obs_with_identity(1, {11, 21}, 1, /*observer=*/5, 3, 0.6f), 1);
    ctx.check(bs.tracks[0].corroboration_count == 1, "same observer does not double-count");
}

static void test_identity_confidence_takes_max(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, 0, /*class_id=*/3, /*id_conf=*/0.9f), 0);
    ctx.check(std::fabs(bs.tracks[0].identity_confidence - 0.9f) < 1e-6f, "initial id_conf");
    // Weaker observation should not overwrite
    bs.update(make_obs_with_identity(1, {11, 21}, 1, 1, /*class_id=*/5, /*id_conf=*/0.3f), 1);
    ctx.check(std::fabs(bs.tracks[0].identity_confidence - 0.9f) < 1e-6f, "weaker obs did not overwrite id_conf");
    ctx.check(bs.tracks[0].class_id == 3, "class_id kept from stronger observation");
    // Stronger observation should overwrite
    bs.update(make_obs_with_identity(1, {12, 22}, 2, 2, /*class_id=*/7, /*id_conf=*/0.95f), 2);
    ctx.check(std::fabs(bs.tracks[0].identity_confidence - 0.95f) < 1e-6f, "stronger obs overwrote id_conf");
    ctx.check(bs.tracks[0].class_id == 7, "class_id updated from stronger observation");
}

static void test_identity_confidence_decays_when_stale(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.confidence_decay_per_second = 0.05f;
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, 0, 3, 0.8f), 0);
    float initial_id_conf = bs.tracks[0].identity_confidence;
    bs.decay(6, 1.0f, cfg);
    ctx.check(bs.tracks[0].identity_confidence < initial_id_conf, "identity_confidence decayed when stale");
    ctx.check(bs.tracks[0].identity_confidence >= 0.0f, "identity_confidence non-negative");
}

static void test_negative_evidence_reduces_identity_confidence(TestContext& ctx) {
    BeliefState bs;
    bs.update(make_obs_with_identity(1, {10, 20}, 0, 0, 3, 0.8f), 0);
    float initial_id_conf = bs.tracks[0].identity_confidence;
    Map map;
    std::vector<EntityId> detected; // empty — target not detected
    bs.apply_negative_evidence({10, 20}, 50.0f, map, detected, 0.3f);
    ctx.check(bs.tracks[0].identity_confidence < initial_id_conf, "negative evidence reduced id_conf");
    float expected = initial_id_conf * 0.7f;
    ctx.check(std::fabs(bs.tracks[0].identity_confidence - expected) < 1e-6f, "correct reduction factor");
}

int main() {
    TestContext ctx;
    std::printf("Running belief tests...\n");
    test_new_observation_creates_fresh_track(ctx);
    test_duplicate_observation_updates_not_duplicates(ctx);
    test_fresh_to_stale_transition(ctx);
    test_stale_to_expired_removal(ctx);
    test_uncertainty_grows_when_stale(ctx);
    test_confidence_decays_when_stale(ctx);
    test_fresh_observation_resets_stale_track(ctx);
    test_multiple_independent_targets(ctx);
    test_confidence_floors_at_zero(ctx);
    test_decay_equivalent_across_dt_for_equal_sim_time(ctx);
    test_new_track_carries_class_and_identity(ctx);
    test_corroboration_increments_on_new_source(ctx);
    test_corroboration_no_double_count_same_source(ctx);
    test_identity_confidence_takes_max(ctx);
    test_identity_confidence_decays_when_stale(ctx);
    test_negative_evidence_reduces_identity_confidence(ctx);
    return ctx.report_and_exit_code();
}
