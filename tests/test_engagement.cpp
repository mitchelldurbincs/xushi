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

static void test_capable_actor_passes_gates() {
    // Actor with full capability, close-range target, fresh track — all gates pass.
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 10;
    actor.cooldown_ticks_remaining = 0;
    actor.team = 0;

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};  // 10m away
    target.team = 1;  // different team

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;
    trk.estimated_position = {60.0f, 50.0f};

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;
    in.world.actor_id = actor.id;
    in.world.track_target_id = target.id;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(out.allowed(), "capable actor passes all gates");
    CHECK(out.failure_mask == 0, "no failure reasons");
}

static void test_same_team_engagement_rejected() {
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 10;
    actor.team = 0;

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};
    target.team = 0;  // same team!

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;
    in.world.actor_id = actor.id;
    in.world.track_target_id = target.id;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(!out.allowed(), "same-team engagement rejected");
    CHECK(has_reason(out.failure_mask, GateFailureReason::FriendlyTarget),
          "FriendlyTarget flagged for same-team");
}

static void test_different_team_no_friendly_target() {
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 10;
    actor.team = 0;

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};
    target.team = 1;  // different team

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;
    in.world.actor_id = actor.id;
    in.world.track_target_id = target.id;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(!has_reason(out.failure_mask, GateFailureReason::FriendlyTarget),
          "no FriendlyTarget for different teams");
}

static void test_cooldown_blocks_engagement() {
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 10;
    actor.cooldown_ticks_remaining = 3;  // on cooldown

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(!out.allowed(), "cooldown blocks engagement");
    CHECK(has_reason(out.failure_mask, GateFailureReason::Cooldown), "Cooldown flagged");
}

static void test_out_of_ammo_blocks_engagement() {
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 0;  // no ammo

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;

    EngagementGateResult out = compute_engagement_gates(in);

    CHECK(!out.allowed(), "out of ammo blocks engagement");
    CHECK(has_reason(out.failure_mask, GateFailureReason::OutOfAmmo), "OutOfAmmo flagged");
}

static void test_friendly_risk_only_flags_same_team() {
    Scenario::EffectProfile profile;
    profile.range = 50.0f;
    profile.requires_los = false;
    profile.identity_threshold = 0.0f;
    profile.corroboration_threshold = 0.0f;
    profile.ammo_cost = 1;

    ScenarioEntity actor;
    actor.id = 1;
    actor.position = {50.0f, 50.0f};
    actor.can_engage = true;
    actor.allowed_effect_profile_indices = {0};
    actor.ammo = 10;
    actor.team = 0;

    ScenarioEntity target;
    target.id = 2;
    target.position = {60.0f, 50.0f};
    target.team = 1;

    // Enemy entity near target — should NOT trigger FriendlyRisk
    ScenarioEntity enemy_bystander;
    enemy_bystander.id = 3;
    enemy_bystander.position = {61.0f, 50.0f};  // 1m from target
    enemy_bystander.team = 1;  // enemy team

    std::vector<ScenarioEntity> entities = {actor, target, enemy_bystander};

    Track trk;
    trk.target = 2;
    trk.status = TrackStatus::FRESH;
    trk.last_update_tick = 5;
    trk.uncertainty = 1.0f;
    trk.identity_confidence = 0.9f;
    trk.corroboration_count = 2;

    EngagementGateInputs in;
    in.actor = &actor;
    in.target_track = &trk;
    in.target_truth = &target;
    in.effect_profile_index = 0;
    in.effect_profile = &profile;
    in.world.tick = 5;
    in.world.actor_id = actor.id;
    in.world.track_target_id = target.id;
    in.world.entities = &entities;

    EngagementGateResult out1 = compute_engagement_gates(in);
    CHECK(!has_reason(out1.failure_mask, GateFailureReason::FriendlyRisk),
          "enemy bystander does not trigger FriendlyRisk");

    // Now add a same-team entity near target — SHOULD trigger FriendlyRisk
    ScenarioEntity friendly_bystander;
    friendly_bystander.id = 4;
    friendly_bystander.position = {61.0f, 50.0f};  // 1m from target
    friendly_bystander.team = 0;  // same team as actor

    entities.push_back(friendly_bystander);

    EngagementGateResult out2 = compute_engagement_gates(in);
    CHECK(has_reason(out2.failure_mask, GateFailureReason::FriendlyRisk),
          "same-team bystander triggers FriendlyRisk");
}

int main() {
    std::printf("Running engagement tests...\n");
    test_engagement_gates_include_track_and_truth_failures();
    test_capable_actor_passes_gates();
    test_same_team_engagement_rejected();
    test_different_team_no_friendly_target();
    test_cooldown_blocks_engagement();
    test_out_of_ammo_blocks_engagement();
    test_friendly_risk_only_flags_same_team();
    TEST_REPORT();
}
