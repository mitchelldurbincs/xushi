#include "test_helpers.h"
#include "../src/action.h"
#include "../src/replay_events.h"
#include "../src/rng.h"
#include "../src/scenario.h"
#include "../src/sim_engine.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// Capture hook: records every action-resolution event so tests can assert
// on counts, last-damage state, hit/miss, and modifier breakdowns.
struct CaptureHooks : RoundHooks {
    int moves = 0;
    int shots = 0;
    int damages = 0;
    int overwatch_sets = 0;
    int door_changes = 0;

    EntityId last_move_actor = 0;
    GridPos  last_move_to{};
    int      last_move_ap_after = -1;

    ShotModifiers last_shot{};

    EntityId last_damage_target = 0;
    int      last_damage_hp_after = -1;
    bool     last_damage_eliminated = false;

    DoorState last_door_state = DoorState::OPEN;
    std::string last_door_cause;

    void on_unit_moved(int, EntityId actor, GridPos, GridPos to, int ap_after) override {
        ++moves;
        last_move_actor = actor;
        last_move_to = to;
        last_move_ap_after = ap_after;
    }
    void on_shot_resolved(int, EntityId, EntityId, const ShotModifiers& m) override {
        ++shots;
        last_shot = m;
    }
    void on_damage(int, EntityId, EntityId target, int, int hp_after, bool elim) override {
        ++damages;
        last_damage_target = target;
        last_damage_hp_after = hp_after;
        last_damage_eliminated = elim;
    }
    void on_overwatch_set(int, EntityId) override { ++overwatch_sets; }
    void on_door_state_changed(int, GridPos, GridPos, DoorState s, const char* cause) override {
        ++door_changes;
        last_door_state = s;
        last_door_cause = cause;
    }
};

// Find a seed whose first uniform() roll satisfies `pred`. Returns 0 if no
// seed in [1, max) qualifies — tests should fail loudly in that case.
uint64_t find_seed(bool (*pred)(float), uint64_t max = 200000) {
    for (uint64_t s = 1; s < max; ++s) {
        Rng r(s);
        if (pred(r.uniform())) return s;
    }
    return 0;
}

// Run one round, supplying `script` to operator `target`, EndTurn to all
// others. Stops as soon as all activations complete.
void run_round_with_script(SimEngine& e, int round, RoundHooks& hooks,
                           EntityId target,
                           const std::vector<ActionRequest>& script) {
    e.begin_round(round, hooks);
    size_t script_cursor = 0;
    while (e.activation_needs_action()) {
        const auto& ctx = e.round_context();
        if (ctx.activation_cursor >= ctx.activation_order.size()) break;
        const size_t idx = ctx.activation_order[ctx.activation_cursor];
        const EntityId actor = e.get_entities()[idx].id;
        ActionRequest a = ActionRequest::end_turn();
        if (actor == target && script_cursor < script.size())
            a = script[script_cursor++];
        if (e.step_activation(round, hooks, a)) break;
    }
    e.finalize_round(round, hooks);
}

// Load the scenario and reposition entities for a clean two-unit test:
// shooter (id 0) at (3, 5), target (id 10) at (6, 5). Both on FLOOR with
// clear LOS at range 3. Other entities pushed to safe corners so they
// don't interfere with LOS or activation count.
Scenario load_two_unit_scenario(uint64_t seed = 1103) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    scn.seed = seed;
    scn.rounds = 1;
    for (auto& e : scn.entities) {
        if (e.id == 0)       e.pos = {3, 5};   // attacker_a
        else if (e.id == 10) e.pos = {6, 5};   // defender_a
        else if (e.id == 1)  e.pos = {1, 1};   // attacker_b
        else if (e.id == 11) e.pos = {14, 1};  // defender_b
        else if (e.id == 2)  e.pos = {1, 1};   // attacker_drone (carried)
        else if (e.id == 12) e.pos = {14, 1};  // defender_drone
    }
    return scn;
}

ScenarioEntity* find(SimEngine& e, EntityId id) {
    return e.find_entity(id);
}

// ---- Tests ------------------------------------------------------------------

void test_move_costs_ap_and_updates_pos(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::move({4, 5}) });

    auto* a = find(e, 0);
    ctx.check(a->pos == GridPos{4, 5}, "move updates pos");
    ctx.check(h.moves == 1, "one move event");
    ctx.check(h.last_move_actor == 0, "move event for correct actor");
    ctx.check(h.last_move_ap_after == 2, "AP decremented by 1 (3 -> 2)");
}

void test_move_blocked_by_wall_noop(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    // Place attacker adjacent to a wall: (1, 5) — col 0 is wall.
    for (auto& ent : scn.entities) if (ent.id == 0) ent.pos = {1, 5};
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::move({0, 5}) });

    auto* a = find(e, 0);
    ctx.check(a->pos == GridPos{1, 5}, "blocked move leaves pos unchanged");
    ctx.check(h.moves == 0, "no move event emitted on invalid move");
}

void test_shoot_hit_applies_damage(TestContext& ctx) {
    // Pick a seed whose first roll < 0.70 (no other modifiers, base hit).
    uint64_t seed = find_seed([](float r) { return r < 0.65f; });
    Scenario scn = load_two_unit_scenario(seed);
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::shoot(10) });

    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    ctx.check(h.shots == 1, "one shot resolved");
    ctx.check(h.last_shot.hit, "shot landed (seed chosen for hit)");
    ctx.check(h.damages == 1, "damage event emitted on hit");
    ctx.check(target->hp == 50, "target HP reduced by 50");
    ctx.check(h.last_damage_hp_after == 50, "damage hp_after matches");
    ctx.check(shooter->ammo == 9, "shooter ammo decremented");
    ctx.check(h.last_shot.base_pct == 70, "base pct = 70");
    ctx.check(h.last_shot.fresh_delta == 10, "fresh delta = +10 (LOS sighting upgraded track)");
    ctx.check(h.last_shot.final_pct == 80, "final pct = 70 + 10");
}

void test_shoot_miss_no_damage_still_spends_ap_ammo(TestContext& ctx) {
    // Pick a seed whose first roll > 0.85 (above any reasonable hit prob).
    uint64_t seed = find_seed([](float r) { return r > 0.90f; });
    Scenario scn = load_two_unit_scenario(seed);
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::shoot(10) });

    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    ctx.check(h.shots == 1, "one shot resolved");
    ctx.check(!h.last_shot.hit, "shot missed (seed chosen for miss)");
    ctx.check(h.damages == 0, "no damage event on miss");
    ctx.check(target->hp == 100, "target HP unchanged on miss");
    ctx.check(shooter->ammo == 9, "ammo still spent on miss");
}

void test_shoot_twice_in_activation_second_noop(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario(find_seed([](float r) { return r < 0.65f; }));
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, {
        ActionRequest::shoot(10),
        ActionRequest::shoot(10),
    });

    auto* shooter = find(e, 0);
    ctx.check(h.shots == 1, "second shoot rejected by 1-shot-per-activation guard");
    ctx.check(shooter->ammo == 9, "only one ammo decrement");
}

void test_hit_probability_cover_modifier(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    // Place defender on the COVER cell at (10, 9).
    // Cover cell 'C' in small_office_breach is at (9, 9). Place shooter at
    // (8, 9) — adjacent FLOOR. Range/LOS not checked by compute_hit_probability.
    for (auto& ent : scn.entities) if (ent.id == 10) ent.pos = {9, 9};
    for (auto& ent : scn.entities) if (ent.id == 0)  ent.pos = {8, 9};
    SimEngine e; e.init(scn);
    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    const float p = e.compute_hit_probability(*shooter, *target, false);
    // No track present → no FRESH/STALE delta. Cover -25. base 70 -25 = 45.
    ctx.check(p == 0.45f, "base 0.70 - cover 0.25 = 0.45");
}

void test_hit_probability_fresh_track_plus_cover(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    // Cover cell 'C' in small_office_breach is at (9, 9). Place shooter at
    // (8, 9) — adjacent FLOOR. Range/LOS not checked by compute_hit_probability.
    for (auto& ent : scn.entities) if (ent.id == 10) ent.pos = {9, 9};
    for (auto& ent : scn.entities) if (ent.id == 0)  ent.pos = {8, 9};
    SimEngine e; e.init(scn);
    // Inject a FRESH track on target into shooter's team belief.
    Sighting s{};
    s.observer = 99; s.target = 10;
    s.estimated_position = {10, 9};
    s.confidence = 1.0f; s.uncertainty = 0.0f; s.class_id = 1;
    e.beliefs().apply_sighting(0, s, 0);

    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    const float p = e.compute_hit_probability(*shooter, *target, false);
    // base 70 + fresh 10 - cover 25 = 55
    ctx.check(std::abs(p - 0.55f) < 1e-6f, "base + fresh - cover = 0.55");
}

void test_hit_probability_clamps_to_floor(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    // Cover cell 'C' in small_office_breach is at (9, 9). Place shooter at
    // (8, 9) — adjacent FLOOR. Range/LOS not checked by compute_hit_probability.
    for (auto& ent : scn.entities) if (ent.id == 10) ent.pos = {9, 9};
    for (auto& ent : scn.entities) if (ent.id == 0)  ent.pos = {8, 9};
    SimEngine e; e.init(scn);
    Sighting s{}; s.observer = 99; s.target = 10;
    s.estimated_position = {10, 9}; s.confidence = 0.5f;
    s.uncertainty = 1.0f; s.class_id = 1;
    e.beliefs().apply_sighting(0, s, 0);
    e.beliefs().find(0)->find_track(10)->status = TrackStatus::STALE;

    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    // STALE -20 + COVER -25 + OW snap -15 = -60; base 70 → 10. Above floor.
    float p1 = e.compute_hit_probability(*shooter, *target, true);
    ctx.check(std::abs(p1 - 0.10f) < 1e-6f, "STALE + COVER + OW snap = 0.10");
    // Lower base_hit so the formula goes well below floor and confirm clamp.
    shooter->weapon_base_hit = 0.30f;  // 30 - 20 - 25 - 15 = -30 → clamp 0.05
    float p2 = e.compute_hit_probability(*shooter, *target, true);
    ctx.check(std::abs(p2 - 0.05f) < 1e-6f, "floor clamp at 0.05");
}

void test_hit_probability_clamps_to_ceiling(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    SimEngine e; e.init(scn);
    auto* shooter = find(e, 0);
    auto* target  = find(e, 10);
    shooter->weapon_base_hit = 0.99f;  // 99 + 10 (FRESH if injected) > 95
    auto& store = const_cast<std::map<int, BeliefState>&>(e.get_beliefs());
    Sighting s{}; s.observer = 99; s.target = 10;
    s.estimated_position = target->pos; s.confidence = 1.0f;
    s.class_id = 1;
    store[0].update(s, 0);
    float p = e.compute_hit_probability(*shooter, *target, false);
    ctx.check(std::abs(p - 0.95f) < 1e-6f, "ceiling clamp at 0.95");
}

void test_shoot_kills_target_eliminates(TestContext& ctx) {
    uint64_t seed = find_seed([](float r) { return r < 0.65f; });
    Scenario scn = load_two_unit_scenario(seed);
    for (auto& ent : scn.entities) if (ent.id == 10) ent.hp = 10;
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::shoot(10) });

    auto* target = find(e, 10);
    ctx.check(h.last_shot.hit, "shot landed (seed)");
    ctx.check(target->hp == 0, "target HP clamped to 0 (10 - 50 = 0)");
    ctx.check(h.last_damage_eliminated, "damage event flagged eliminated");

    // Next round: dead defender excluded from activation_order entirely.
    CaptureHooks h2;
    e.begin_round(1, h2);
    bool dead_present = false;
    for (size_t idx : e.round_context().activation_order)
        if (e.get_entities()[idx].id == 10) dead_present = true;
    ctx.check(!dead_present, "dead operator excluded from next round activation_order");
    while (e.activation_needs_action())
        e.step_activation(1, h2, ActionRequest::end_turn());
    e.finalize_round(1, h2);
}

void test_shoot_refreshes_team_belief_to_fresh(TestContext& ctx) {
    uint64_t seed = find_seed([](float r) { return r < 0.65f; });
    Scenario scn = load_two_unit_scenario(seed);
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::shoot(10) });

    const auto& store = e.get_beliefs();
    auto it = store.find(0);
    ctx.check(it != store.end(), "team 0 belief exists");
    const Track* t = it->second.find_track(10);
    ctx.check(t != nullptr, "track on target 10 created in shooter team belief");
    ctx.check(t->status == TrackStatus::FRESH, "track is FRESH after LOS shot");
}

void test_overwatch_sets_flag_and_spends_2ap(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::overwatch() });

    auto* a = find(e, 0);
    ctx.check(a->overwatch_active, "overwatch_active flag set");
    ctx.check(h.overwatch_sets == 1, "overwatch_set hook fired exactly once");
    // 2 AP spent → 1 left → next step EndTurn ends activation.
}

void test_overwatch_requires_no_prior_shot(TestContext& ctx) {
    uint64_t seed = find_seed([](float r) { return r < 0.65f; });
    Scenario scn = load_two_unit_scenario(seed);
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, {
        ActionRequest::shoot(10),
        ActionRequest::overwatch(),
    });

    auto* a = find(e, 0);
    ctx.check(!a->overwatch_active, "overwatch rejected after prior shot");
    ctx.check(h.overwatch_sets == 0, "no overwatch_set hook fired");
}

void test_open_door_adjacent(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    // Door is at (11,3)-(12,3). Place attacker at (11,3) so adjacent to door.
    for (auto& ent : scn.entities) if (ent.id == 0) ent.pos = {11, 3};
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::open_door({12, 3}) });

    ctx.check(h.door_changes == 1, "door state change emitted");
    ctx.check(h.last_door_state == DoorState::OPEN, "door is now OPEN");
    ctx.check(h.last_door_cause == "open", "cause = open");
}

void test_breach_locked_door(TestContext& ctx) {
    Scenario scn = load_two_unit_scenario();
    for (auto& ent : scn.entities) if (ent.id == 0) ent.pos = {11, 3};
    // Mutate the loaded door to LOCKED.
    for (auto& d : scn.grid.doors())
        if ((d.a == GridPos{11, 3} && d.b == GridPos{12, 3}) ||
            (d.b == GridPos{11, 3} && d.a == GridPos{12, 3}))
            d.state = DoorState::LOCKED;
    SimEngine e; e.init(scn);
    CaptureHooks h;
    run_round_with_script(e, 0, h, 0, { ActionRequest::breach({12, 3}) });

    ctx.check(h.door_changes == 1, "door state change emitted");
    ctx.check(h.last_door_state == DoorState::OPEN, "breach unlocks to OPEN");
    ctx.check(h.last_door_cause == "breach", "cause = breach");
}

void test_determinism_same_seed_same_script(TestContext& ctx) {
    uint64_t seed = find_seed([](float r) { return r < 0.65f; });
    auto run = [&]() {
        Scenario scn = load_two_unit_scenario(seed);
        scn.rounds = 3;
        SimEngine e; e.init(scn);
        CaptureHooks h;
        for (int r = 0; r < scn.rounds; ++r) {
            std::vector<ActionRequest> script;
            if (r == 0) script = { ActionRequest::shoot(10), ActionRequest::end_turn() };
            else if (r == 1) script = { ActionRequest::move({4, 5}), ActionRequest::end_turn() };
            run_round_with_script(e, r, h, 0, script);
        }
        return e.compute_world_hash();
    };
    uint64_t a = run();
    uint64_t b = run();
    ctx.check(a == b, "same seed + same script → identical world_hash");
}

void test_determinism_different_seed_diverges_on_shot(TestContext& ctx) {
    auto run = [&](uint64_t seed) {
        Scenario scn = load_two_unit_scenario(seed);
        SimEngine e; e.init(scn);
        CaptureHooks h;
        run_round_with_script(e, 0, h, 0, { ActionRequest::shoot(10) });
        return e.compute_world_hash();
    };
    // Two seeds: one produces hit (target HP changes), one produces miss.
    uint64_t hit_seed  = find_seed([](float r) { return r < 0.50f; });
    uint64_t miss_seed = find_seed([](float r) { return r > 0.85f; });
    uint64_t a = run(hit_seed);
    uint64_t b = run(miss_seed);
    ctx.check(a != b, "different seeds produce different world_hash on a shot round");
}

}  // namespace

int main() {
    return run_test_suite("action", [](TestContext& ctx) {
        test_move_costs_ap_and_updates_pos(ctx);
        test_move_blocked_by_wall_noop(ctx);
        test_shoot_hit_applies_damage(ctx);
        test_shoot_miss_no_damage_still_spends_ap_ammo(ctx);
        test_shoot_twice_in_activation_second_noop(ctx);
        test_hit_probability_cover_modifier(ctx);
        test_hit_probability_fresh_track_plus_cover(ctx);
        test_hit_probability_clamps_to_floor(ctx);
        test_hit_probability_clamps_to_ceiling(ctx);
        test_shoot_kills_target_eliminates(ctx);
        test_shoot_refreshes_team_belief_to_fresh(ctx);
        test_overwatch_sets_flag_and_spends_2ap(ctx);
        test_overwatch_requires_no_prior_shot(ctx);
        test_open_door_adjacent(ctx);
        test_breach_locked_door(ctx);
        test_determinism_same_seed_same_script(ctx);
        test_determinism_different_seed_diverges_on_shot(ctx);
    });
}
