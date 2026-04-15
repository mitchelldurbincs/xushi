#include "test_helpers.h"
#include "../src/belief.h"

static Sighting make_sighting(EntityId observer, EntityId target, GridPos pos,
                              float conf = 1.0f, float unc = 0.0f) {
    Sighting s;
    s.observer = observer;
    s.target = target;
    s.estimated_position = pos;
    s.confidence = conf;
    s.uncertainty = unc;
    s.class_id = 1;
    s.is_spoof = false;
    return s;
}

static void test_update_creates_fresh_track(TestContext& ctx) {
    BeliefState b;
    b.update(make_sighting(10, 20, {3, 4}, 0.9f, 1.0f), 0);
    auto* t = b.find_track(20);
    ctx.check(t != nullptr, "track created");
    ctx.check(t->status == TrackStatus::FRESH, "new track is FRESH");
    ctx.check(t->estimated_position == GridPos{3, 4}, "position set");
    ctx.check(t->confidence == 0.9f, "confidence set");
}

static void test_update_refreshes_existing_track(TestContext& ctx) {
    BeliefState b;
    b.update(make_sighting(10, 20, {1, 1}, 0.6f, 2.0f), 0);
    b.update(make_sighting(11, 20, {5, 6}, 0.95f, 0.5f), 3);
    auto* t = b.find_track(20);
    ctx.check(t->estimated_position == GridPos{5, 6}, "position refreshed");
    ctx.check(t->last_update_round == 3, "last_update_round refreshed");
    ctx.check(t->confidence == 0.95f, "confidence refreshed");
}

static void test_decay_transitions_fresh_to_stale(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_rounds = 2;
    cfg.stale_rounds = 3;
    cfg.confidence_decay_per_round = 0.1f;
    cfg.uncertainty_growth_per_round = 0.5f;

    BeliefState b;
    b.update(make_sighting(10, 20, {0, 0}, 1.0f, 0.0f), 0);
    b.decay(1, cfg);
    ctx.check(b.find_track(20)->status == TrackStatus::FRESH,
              "FRESH within fresh_rounds");

    b.decay(3, cfg);
    ctx.check(b.find_track(20)->status == TrackStatus::STALE,
              "transitions to STALE after fresh_rounds");
    ctx.check(b.find_track(20)->confidence < 1.0f,
              "confidence decays during STALE");
}

static void test_decay_expires_old_track(TestContext& ctx) {
    BeliefConfig cfg;
    cfg.fresh_rounds = 1;
    cfg.stale_rounds = 2;

    BeliefState b;
    b.update(make_sighting(1, 2, {0, 0}), 0);
    b.decay(10, cfg);
    ctx.check(b.find_track(2) == nullptr, "very old track expires and is removed");
}

static void test_clear_spoofs(TestContext& ctx) {
    BeliefState b;
    Sighting real = make_sighting(10, 20, {3, 3});
    b.update(real, 0);
    Sighting spoof;
    spoof.observer = 0;
    spoof.target = 99;
    spoof.estimated_position = {5, 5};
    spoof.confidence = 0.6f;
    spoof.is_spoof = true;
    b.update(spoof, 0);

    ctx.check(b.find_track(20) != nullptr, "real track present");
    ctx.check(b.find_track(99) != nullptr, "spoof track present");

    b.clear_spoofs_in({5, 5}, 1);
    ctx.check(b.find_track(99) == nullptr, "spoof cleared by drone scan");
    ctx.check(b.find_track(20) != nullptr, "real track untouched");
}

int main() {
    return run_test_suite("belief", [](TestContext& ctx) {
        test_update_creates_fresh_track(ctx);
        test_update_refreshes_existing_track(ctx);
        test_decay_transitions_fresh_to_stale(ctx);
        test_decay_expires_old_track(ctx);
        test_clear_spoofs(ctx);
    });
}
