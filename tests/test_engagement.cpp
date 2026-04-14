#include "test_helpers.h"
#include "../src/engagement.h"
#include "../src/sim_engine.h"

static void test_engagement_gates_include_track_and_truth_failures(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(!out.allowed(), "engagement rejected");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::NoCapability), "includes capability gate");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::TrackTooStale), "flags stale tracks");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::TrackTooUncertain), "flags uncertainty");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::IdentityTooWeak), "flags identity");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::NeedsCorroboration), "flags corroboration");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::OutOfRange), "flags range");

    ctx.check(out.debug.track_age_ticks.has_value(), "debug has age");
    ctx.check(out.debug.track_age_ticks.value() == 7, "age derived from world tick");
    ctx.check(out.debug.actor_to_truth_distance.has_value(), "debug has distance");
}

static void test_capable_actor_passes_gates(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(out.allowed(), "capable actor passes all gates");
    ctx.check(out.failure_mask == 0, "no failure reasons");
}

static void test_same_team_engagement_rejected(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(!out.allowed(), "same-team engagement rejected");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::FriendlyTarget),
          "FriendlyTarget flagged for same-team");
}

static void test_different_team_no_friendly_target(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(!has_reason(out.failure_mask, GateFailureReason::FriendlyTarget),
          "no FriendlyTarget for different teams");
}

static void test_cooldown_blocks_engagement(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(!out.allowed(), "cooldown blocks engagement");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::Cooldown), "Cooldown flagged");
}

static void test_out_of_ammo_blocks_engagement(TestContext& ctx) {
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

    EngagementGateResult out = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);

    ctx.check(!out.allowed(), "out of ammo blocks engagement");
    ctx.check(has_reason(out.failure_mask, GateFailureReason::OutOfAmmo), "OutOfAmmo flagged");
}

static void test_friendly_risk_only_flags_same_team(TestContext& ctx) {
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

    EngagementGateResult out1 = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);
    ctx.check(!has_reason(out1.failure_mask, GateFailureReason::FriendlyRisk),
          "enemy bystander does not trigger FriendlyRisk");

    // Now add a same-team entity near target — SHOULD trigger FriendlyRisk
    ScenarioEntity friendly_bystander;
    friendly_bystander.id = 4;
    friendly_bystander.position = {61.0f, 50.0f};  // 1m from target
    friendly_bystander.team = 0;  // same team as actor

    entities.push_back(friendly_bystander);

    EngagementGateResult out2 = compute_engagement_gates(in, EngagementGateStage::TruthAdjudication);
    ctx.check(has_reason(out2.failure_mask, GateFailureReason::FriendlyRisk),
          "same-team bystander triggers FriendlyRisk");
}

// ── Engagement stop (collision) tests ──

// Helper: build a minimal scenario with two armed attackers facing each other
static Scenario make_collision_scenario(float distance) {
    Scenario s;
    s.seed = 42;
    s.dt = 1.0f;
    s.ticks = 100;
    s.max_sensor_range = 200.0f;

    Scenario::EffectProfile weapon;
    weapon.name = "weapon";
    weapon.range = 30.0f;
    weapon.requires_los = false;
    weapon.identity_threshold = 0.0f;
    weapon.corroboration_threshold = 0.0f;
    weapon.hit_probability = 1.0f;  // always hit for deterministic tests
    weapon.vitality_delta_min = -20;
    weapon.vitality_delta_max = -20;
    weapon.cooldown_ticks = 3;
    weapon.ammo_cost = 1;
    s.effect_profiles.push_back(weapon);

    // Attacker on team 0
    ScenarioEntity atk0;
    atk0.id = 0;
    atk0.role_name = "attacker";
    atk0.team = 0;
    atk0.position = {0, 50};
    atk0.velocity = {0, 0};
    atk0.can_sense = true;
    atk0.can_track = true;
    atk0.can_engage = true;
    atk0.is_observable = true;
    atk0.speed = 5.0f;
    atk0.ammo = 20;
    atk0.vitality = 100;
    atk0.max_vitality = 100;
    atk0.allowed_effect_profile_indices = {0};
    atk0.waypoints = {{distance, 50}};
    atk0.waypoint_mode = ScenarioEntity::WaypointMode::Stop;
    s.entities.push_back(atk0);

    // Attacker on team 1
    ScenarioEntity atk1;
    atk1.id = 1;
    atk1.role_name = "attacker";
    atk1.team = 1;
    atk1.position = {distance, 50};
    atk1.velocity = {0, 0};
    atk1.can_sense = true;
    atk1.can_track = true;
    atk1.can_engage = true;
    atk1.is_observable = true;
    atk1.speed = 5.0f;
    atk1.ammo = 20;
    atk1.vitality = 100;
    atk1.max_vitality = 100;
    atk1.allowed_effect_profile_indices = {0};
    atk1.waypoints = {{0, 50}};
    atk1.waypoint_mode = ScenarioEntity::WaypointMode::Stop;
    s.entities.push_back(atk1);

    return s;
}

static void test_engagement_stop_within_range(TestContext& ctx) {
    // Two armed enemies start within weapon range (20 apart, range 30).
    // They should NOT move on any tick.
    Scenario scn = make_collision_scenario(20.0f);

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;

    Vec2 pos0_before = engine.get_entities()[0].position;
    Vec2 pos1_before = engine.get_entities()[1].position;

    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    Vec2 pos0_after = engine.get_entities()[0].position;
    Vec2 pos1_after = engine.get_entities()[1].position;

    ctx.check(pos0_after.x == pos0_before.x && pos0_after.y == pos0_before.y,
          "engagement stop: entity 0 holds position");
    ctx.check(pos1_after.x == pos1_before.x && pos1_after.y == pos1_before.y,
          "engagement stop: entity 1 holds position");
}

static void test_engagement_stop_same_team_ignored(TestContext& ctx) {
    // Two armed entities on the SAME team within weapon range should NOT stop.
    Scenario scn = make_collision_scenario(20.0f);
    scn.entities[1].team = 0;  // same team

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;

    Vec2 pos0_before = engine.get_entities()[0].position;

    engine.step(0, hooks);

    Vec2 pos0_after = engine.get_entities()[0].position;
    ctx.check(pos0_after.x != pos0_before.x,
          "same-team entities continue moving");
}

static void test_dead_entity_skips_movement(TestContext& ctx) {
    // An entity with vitality=0 should not move.
    Scenario scn = make_collision_scenario(100.0f);
    scn.entities[0].vitality = 0;

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;

    Vec2 pos_before = engine.get_entities()[0].position;

    for (int t = 0; t < 5; ++t)
        engine.step(t, hooks);

    Vec2 pos_after = engine.get_entities()[0].position;
    ctx.check(pos_after.x == pos_before.x && pos_after.y == pos_before.y,
          "dead entity does not move");
}

static void test_dead_enemy_does_not_block(TestContext& ctx) {
    // A dead armed enemy should NOT trigger engagement stop.
    Scenario scn = make_collision_scenario(20.0f);
    scn.entities[1].vitality = 0;  // enemy is dead

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;

    Vec2 pos0_before = engine.get_entities()[0].position;

    engine.step(0, hooks);

    Vec2 pos0_after = engine.get_entities()[0].position;
    ctx.check(pos0_after.x != pos0_before.x,
          "dead enemy does not block movement");
}

static void test_attacker_combat_causes_death(TestContext& ctx) {
    // Two attackers within range: one should eventually reach 0 vitality.
    // weapon: 100% hit, -20 damage, cooldown 3, ammo 20
    // 100 HP / 20 damage = 5 hits to kill. At 1 hit per 4 ticks (cooldown 3 + 1 for action timing),
    // death should occur within ~25 ticks.
    Scenario scn = make_collision_scenario(20.0f);

    SimEngine engine;
    engine.init(scn);
    TickHooks hooks;

    // Submit engagement actions each tick (mimicking main.cpp auto-engage)
    for (int t = 0; t < 30; ++t) {
        for (const auto& e : engine.get_entities()) {
            if (!e.can_engage || e.vitality <= 0) continue;
            if (e.cooldown_ticks_remaining > 0) continue;
            auto bit = engine.get_beliefs().find(e.id);
            if (bit == engine.get_beliefs().end()) continue;
            for (const auto& trk : bit->second.tracks) {
                if (trk.status != TrackStatus::FRESH) continue;
                const ScenarioEntity* target = nullptr;
                for (const auto& te : engine.get_entities()) {
                    if (te.id == trk.target) { target = &te; break; }
                }
                if (target && e.team >= 0 && target->team >= 0 && e.team == target->team)
                    continue;
                ActionRequest req;
                req.actor = e.id;
                req.type = ActionType::EngageTrack;
                req.track_target = trk.target;
                req.effect_profile_index = 0;
                engine.submit_action(req);
                break;
            }
        }
        engine.step(t, hooks);
    }

    bool someone_died = false;
    for (const auto& e : engine.get_entities()) {
        if (e.can_engage && e.vitality <= 0)
            someone_died = true;
    }
    ctx.check(someone_died, "prolonged combat causes at least one death");
}

int main() {
    TestContext ctx;
    std::printf("Running engagement tests...\n");
    test_engagement_gates_include_track_and_truth_failures(ctx);
    test_capable_actor_passes_gates(ctx);
    test_same_team_engagement_rejected(ctx);
    test_different_team_no_friendly_target(ctx);
    test_cooldown_blocks_engagement(ctx);
    test_out_of_ammo_blocks_engagement(ctx);
    test_friendly_risk_only_flags_same_team(ctx);

    // Engagement stop (collision) tests
    test_engagement_stop_within_range(ctx);
    test_engagement_stop_same_team_ignored(ctx);
    test_dead_entity_skips_movement(ctx);
    test_dead_enemy_does_not_block(ctx);
    test_attacker_combat_causes_death(ctx);

    return ctx.report_and_exit_code();
}
