#include "../src/belief.h"
#include <cstdio>
#include <cmath>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(expr, name) do { \
    tests_run++; \
    if (expr) { \
        tests_passed++; \
        std::printf("  PASS  %s\n", name); \
    } else { \
        std::printf("  FAIL  %s\n", name); \
    } \
} while(0)

static Observation make_obs(EntityId target, Vec2 pos, int tick) {
    return {tick, 0, target, pos, 1.0f, 0.8f};
}

static void test_new_observation_creates_fresh_track() {
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    CHECK(bs.tracks.size() == 1, "one track created");
    CHECK(bs.tracks[0].target == 1, "correct target id");
    CHECK(bs.tracks[0].status == TrackStatus::FRESH, "status is FRESH");
}

static void test_duplicate_observation_updates_not_duplicates() {
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.update(make_obs(1, {15, 25}, 5), 5);
    CHECK(bs.tracks.size() == 1, "still one track after second obs");
    CHECK(bs.tracks[0].estimated_position.x == 15.0f, "position updated");
    CHECK(bs.tracks[0].last_update_tick == 5, "tick updated");
}

static void test_fresh_to_stale_transition() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);

    bs.decay(5, cfg);
    CHECK(bs.tracks[0].status == TrackStatus::FRESH, "still fresh at boundary");

    bs.decay(6, cfg);
    CHECK(bs.tracks[0].status == TrackStatus::STALE, "stale after fresh_ticks");
}

static void test_stale_to_expired_removal() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.stale_ticks = 10;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);

    bs.decay(15, cfg);
    CHECK(bs.tracks.size() == 1, "still alive at stale boundary");

    bs.decay(16, cfg);
    CHECK(bs.tracks.empty(), "expired and removed");
}

static void test_uncertainty_grows_when_stale() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.uncertainty_growth_rate = 0.5f;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    float initial_unc = bs.tracks[0].uncertainty;

    bs.decay(6, cfg); // one tick into stale
    CHECK(bs.tracks[0].uncertainty > initial_unc, "uncertainty grew");
}

static void test_confidence_decays_when_stale() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.confidence_decay_rate = 0.05f;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    float initial_conf = bs.tracks[0].confidence;

    bs.decay(6, cfg);
    CHECK(bs.tracks[0].confidence < initial_conf, "confidence decayed");
}

static void test_fresh_observation_resets_stale_track() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);

    bs.decay(8, cfg);
    CHECK(bs.tracks[0].status == TrackStatus::STALE, "stale before refresh");

    bs.update(make_obs(1, {30, 40}, 8), 8);
    CHECK(bs.tracks[0].status == TrackStatus::FRESH, "back to fresh after update");
    CHECK(bs.tracks[0].estimated_position.x == 30.0f, "position refreshed");
}

static void test_multiple_independent_targets() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 5;
    cfg.stale_ticks = 10;
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);
    bs.update(make_obs(2, {30, 40}, 0), 0);
    CHECK(bs.tracks.size() == 2, "two tracks for two targets");

    // Refresh only target 1
    bs.update(make_obs(1, {15, 25}, 10), 10);

    bs.decay(16, cfg);
    // Target 1 was refreshed at tick 10, age=6 -> STALE
    // Target 2 last updated at tick 0, age=16 -> EXPIRED
    CHECK(bs.tracks.size() == 1, "expired target removed, other survives");
    CHECK(bs.tracks[0].target == 1, "surviving track is target 1");
}

static void test_confidence_floors_at_zero() {
    BeliefConfig cfg;
    cfg.fresh_ticks = 0;
    cfg.stale_ticks = 100;
    cfg.confidence_decay_rate = 1.0f; // aggressive decay
    BeliefState bs;
    bs.update(make_obs(1, {10, 20}, 0), 0);

    bs.decay(5, cfg);
    CHECK(bs.tracks[0].confidence >= 0.0f, "confidence doesn't go negative");
}

int main() {
    std::printf("Running belief tests...\n");

    test_new_observation_creates_fresh_track();
    test_duplicate_observation_updates_not_duplicates();
    test_fresh_to_stale_transition();
    test_stale_to_expired_removal();
    test_uncertainty_grows_when_stale();
    test_confidence_decays_when_stale();
    test_fresh_observation_resets_stale_track();
    test_multiple_independent_targets();
    test_confidence_floors_at_zero();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
