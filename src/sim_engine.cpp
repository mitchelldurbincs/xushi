#include "sim_engine.h"

#include "constants.h"
#include "world_hash.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

double elapsed_us(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// Clamp helper (std::clamp is C++17 but needs <algorithm>; keep it local for
// clarity and to pin the [0.05, 0.95] contract invariant in one place).
inline float clamp_hit(float p) {
    if (p < 0.05f) return 0.05f;
    if (p > 0.95f) return 0.95f;
    return p;
}
}  // namespace

void SimEngine::init(const Scenario& scn, GameMode* game_mode) {
    scn_ = &scn;
    grid_ = scn.grid;
    entities_ = scn.entities;
    belief_config_ = scn.belief;

    team_ids_.clear();
    std::set<int> team_set;
    for (const auto& e : entities_)
        if (e.team >= 0) team_set.insert(e.team);
    team_ids_.assign(team_set.begin(), team_set.end());

    rng_ = Rng(scn.seed);
    comms_.clear();
    beliefs_.init_teams(team_ids_);

    game_mode_ = game_mode;
    last_game_mode_result_ = GameModeResult{};
    if (game_mode_)
        game_mode_->init(scn, entities_);

    round_state_ = RoundState{};
    round_context_ = RoundContext{};
}

ScenarioEntity* SimEngine::find_entity(EntityId id) {
    for (auto& e : entities_)
        if (e.id == id) return &e;
    return nullptr;
}

const ScenarioEntity* SimEngine::find_entity(EntityId id) const {
    for (const auto& e : entities_)
        if (e.id == id) return &e;
    return nullptr;
}

uint64_t SimEngine::compute_world_hash() const {
    return compute_world_hash_canonical(entities_, beliefs_.states());
}

void SimEngine::require_phase(RoundPhase expected) const {
    if (round_state_.phase != expected)
        throw std::logic_error("illegal round phase transition");
}

void SimEngine::reset_round_context(int round) {
    round_context_ = RoundContext{};
    round_context_.round_number = round;
    round_state_.round_number = round;

    // Initiative alternates by round across active teams (contract §2).
    // Team 0 has initiative on round 0 ("attacker on round 1" 1-indexed).
    if (!team_ids_.empty()) {
        const size_t n = team_ids_.size();
        const size_t i = static_cast<size_t>(round) % n;
        round_context_.initiative_team = team_ids_[i];
        round_state_.initiative_team = team_ids_[i];
    } else {
        round_context_.initiative_team = -1;
        round_state_.initiative_team = -1;
    }

    // Refresh operator AP (contract Phase 1).
    for (const auto& e : entities_) {
        if (e.hp <= 0) continue;
        if (e.kind != EntityKind::Operator) continue;
        round_context_.operator_ap_max[e.id] = e.max_ap;
        round_context_.operator_ap[e.id] = e.max_ap;
    }
    // moved_this_round is scoped to the round (contract §4 modifier).
    // It's already default-empty from the RoundContext reset above.

    // Per-activation scratch resets when the cursor advances to a new actor
    // (see advance_to_next_activation). Clear it here too for safety.
    activation_state_ = ActivationState{};
    // Refresh per-team support AP (contract Phase 1).
    for (int team : team_ids_) {
        round_context_.support_ap_max[team] = kTeamSupportApMax;
        round_context_.support_ap[team] = kTeamSupportApMax;
        round_context_.support_ap_spent[team] = 0;
    }

    build_activation_order_for_round();
    round_state_.activation_index = 0;
    round_context_.activation_cursor = 0;
}

void SimEngine::build_activation_order_for_round() {
    round_context_.activation_order.clear();
    std::vector<size_t> same_team;
    std::vector<size_t> other_teams;
    for (size_t i = 0; i < entities_.size(); ++i) {
        const auto& e = entities_[i];
        if (e.hp <= 0) continue;
        if (e.kind != EntityKind::Operator) continue;  // drones don't activate
        if (e.team == round_context_.initiative_team) same_team.push_back(i);
        else other_teams.push_back(i);
    }
    round_context_.activation_order.insert(round_context_.activation_order.end(),
                                           same_team.begin(), same_team.end());
    round_context_.activation_order.insert(round_context_.activation_order.end(),
                                           other_teams.begin(), other_teams.end());
}

void SimEngine::phase_round_start() {
    // 1. AP refresh already handled in reset_round_context.
    // 4. Decrement timed effects: comms jams.
    comms_.tick_down();
    // 6. Age tracks.
    for (auto& [team, belief] : beliefs_.states())
        belief.decay(round_context_.round_number, belief_config_);
}

void SimEngine::phase_support() {
    // Support phase is a placeholder: drone/cyber actions are not yet
    // wired. Reserved for the next migration step.
}

void SimEngine::phase_round_end(int round, RoundHooks& hooks) {
    // Decay tracks one more time for end-of-round consistency.
    for (auto& [team, belief] : beliefs_.states())
        belief.decay(round, belief_config_);

    if (game_mode_) {
        last_game_mode_result_ = game_mode_->on_round_end(round, entities_);
        if (last_game_mode_result_.finished)
            hooks.on_game_mode_end(round, last_game_mode_result_);
    }

    uint64_t hash = compute_world_hash();
    hooks.on_world_hash(round, hash);
    hooks.on_round_ended(round);
}

void SimEngine::begin_round(int round, RoundHooks& hooks) {
    require_phase(RoundPhase::Idle);
    reset_round_context(round);
    advance_phase(RoundPhase::RoundStart);

    if (game_mode_)
        game_mode_->on_round_start(round, entities_);

    auto run_phase = [&](const char* name, auto&& fn) {
        auto t0 = Clock::now();
        fn();
        hooks.on_phase_timing(name, elapsed_us(t0));
    };

    run_phase("round_start", [&] { phase_round_start(); });

    advance_phase(RoundPhase::SupportPhase);
    run_phase("support", [&] { phase_support(); });

    advance_phase(RoundPhase::Activations);
    hooks.on_round_started(round, round_context_.initiative_team);
}

void SimEngine::advance_to_next_activation() {
    // Move cursor over dead operators (contract §10 — mid-round elimination
    // means a slot in activation_order can now be invalid). When we find
    // a live operator, prime activation_state_ for them and clear their
    // overwatch (contract §7: "persists until the operator's next
    // activation").
    while (round_context_.activation_cursor < round_context_.activation_order.size()) {
        const size_t idx = round_context_.activation_order[round_context_.activation_cursor];
        ScenarioEntity& e = entities_[idx];
        if (e.hp <= 0) {
            ++round_context_.activation_cursor;
            continue;
        }
        activation_state_ = ActivationState{};
        activation_state_.actor = e.id;
        activation_state_.started = true;
        e.overwatch_active = false;
        return;
    }
    activation_state_ = ActivationState{};  // past the end
}

bool SimEngine::activation_needs_action() const {
    if (round_state_.phase != RoundPhase::Activations) return false;
    return round_context_.activation_cursor < round_context_.activation_order.size();
}

float SimEngine::compute_hit_probability(const ScenarioEntity& shooter,
                                         const ScenarioEntity& target,
                                         bool overwatch_snap) const {
    // Contract §4 hit probability formula. All modifiers stack additively,
    // clamped to [5%, 95%]. FRESH +10, STALE -20 are looked up on the
    // shooter's *team* belief (contract §8 — tracks are team-scoped).
    float p = shooter.weapon_base_hit;

    const BeliefState* belief = beliefs_.find(shooter.team);
    if (belief) {
        const Track* trk = belief->find_track(target.id);
        if (trk) {
            if (trk->status == TrackStatus::FRESH) p += 0.10f;
            else if (trk->status == TrackStatus::STALE) p -= 0.20f;
        }
    }
    // Drones flying over COVER cells get no cover bonus (contract §5).
    if (target.kind == EntityKind::Operator &&
        grid_.in_bounds(target.pos) &&
        grid_.cell(target.pos) == CellType::COVER) {
        p -= 0.25f;
    }
    if (overwatch_snap) p -= 0.15f;
    if (round_context_.moved_this_round.count(target.id) > 0) p -= 0.10f;

    return clamp_hit(p);
}

bool SimEngine::apply_move(int round, RoundHooks& hooks,
                           ScenarioEntity& actor, GridPos target) {
    if (chebyshev_distance(actor.pos, target) != 1) return false;
    if (!grid_.passable(target)) return false;
    if (!grid_.edge_passable(actor.pos, target)) return false;
    auto& ap = round_context_.operator_ap[actor.id];
    if (ap < 1) return false;
    for (const auto& e : entities_)
        if (e.id != actor.id && e.hp > 0 && e.pos == target) return false;

    const GridPos from = actor.pos;
    actor.pos = target;
    --ap;
    round_context_.moved_this_round.insert(actor.id);

    hooks.on_unit_moved(round, actor.id, from, target, ap);
    return true;
}

bool SimEngine::resolve_shot(int round, RoundHooks& hooks,
                             ScenarioEntity& shooter, EntityId target_id,
                             bool overwatch_snap) {
    // 1 shot per activation (contract §4).
    if (activation_state_.shot_this_activation) return false;
    auto& ap = round_context_.operator_ap[shooter.id];
    if (ap < 1) return false;
    if (shooter.ammo < 1) return false;

    ScenarioEntity* target = find_entity(target_id);
    if (!target) return false;
    if (target->hp <= 0) return false;
    if (target->team == shooter.team) return false;  // no friendly fire

    // Range + LOS gate.
    if (chebyshev_distance(shooter.pos, target->pos) > shooter.weapon_range) return false;
    if (!grid_.line_of_sight(shooter.pos, target->pos)) return false;

    // Gunshot is a loud, LOS-confirmed sighting: refresh the shooter's team
    // belief to FRESH on the target *before* the modifier lookup so §4's
    // FRESH bonus applies. §8 noise effects on the opposing team are
    // migration Step 5 (deferred).
    Sighting s{};
    s.observer = shooter.id;
    s.target = target->id;
    s.estimated_position = target->pos;
    s.confidence = 1.0f;
    s.uncertainty = 0.0f;
    s.class_id = (target->kind == EntityKind::Drone) ? 2 : 1;
    beliefs_.apply_sighting(shooter.team, s, round);

    // Compute hit probability, convert to integer percent-points for
    // replay / deterministic hashing (contract §14: integer-only inputs).
    const float final_p = compute_hit_probability(shooter, *target, overwatch_snap);
    // Unpack deltas for replay (re-derive rather than threading state).
    ShotModifiers mods{};
    mods.base_pct = static_cast<int>(std::lround(shooter.weapon_base_hit * 100.0f));
    const BeliefState* belief = beliefs_.find(shooter.team);
    if (belief) {
        if (const Track* trk = belief->find_track(target->id)) {
            if (trk->status == TrackStatus::FRESH) mods.fresh_delta = 10;
            else if (trk->status == TrackStatus::STALE) mods.stale_delta = -20;
        }
    }
    if (target->kind == EntityKind::Operator &&
        grid_.in_bounds(target->pos) &&
        grid_.cell(target->pos) == CellType::COVER) {
        mods.cover_delta = -25;
    }
    if (overwatch_snap) mods.overwatch_delta = -15;
    if (round_context_.moved_this_round.count(target->id) > 0) mods.moved_delta = -10;
    mods.final_pct = static_cast<int>(std::lround(final_p * 100.0f));

    // RNG draw — the *only* RNG consumer in Step 2. Happens exactly once
    // per resolved shot (post all validation) to preserve the §14
    // determinism contract: same seed + same action script ⇒ byte-identical
    // world hash.
    const float roll = rng_.uniform();
    mods.rolled_pct = static_cast<int>(std::lround(roll * 100.0f));
    mods.hit = (roll < final_p);

    --ap;
    --shooter.ammo;
    activation_state_.shot_this_activation = true;

    hooks.on_shot_resolved(round, shooter.id, target->id, mods);

    if (mods.hit) {
        const int hp_before = target->hp;
        target->hp = std::max(0, target->hp - shooter.weapon_damage);
        const bool eliminated = (hp_before > 0 && target->hp == 0);
        hooks.on_damage(round, shooter.id, target->id,
                        shooter.weapon_damage, target->hp, eliminated);
        if (game_mode_)
            game_mode_->on_entity_damaged(round, target->id, hp_before, target->hp);
    }
    return true;
}

bool SimEngine::set_overwatch(int round, RoundHooks& hooks, ScenarioEntity& actor) {
    // Contract §7: 2 AP, must not have fired this activation.
    if (activation_state_.shot_this_activation) return false;
    auto& ap = round_context_.operator_ap[actor.id];
    if (ap < 2) return false;
    if (actor.overwatch_active) return false;  // idempotent guard
    actor.overwatch_active = true;
    ap -= 2;
    hooks.on_overwatch_set(round, actor.id);
    return true;
}

bool SimEngine::apply_door_action(int round, RoundHooks& hooks,
                                  ScenarioEntity& actor, GridPos target,
                                  ActionKind kind) {
    if (chebyshev_distance(actor.pos, target) != 1) return false;
    auto& ap = round_context_.operator_ap[actor.id];
    if (ap < 1) return false;
    const int idx = grid_.find_door(actor.pos, target);
    if (idx < 0) return false;
    auto& doors = grid_.doors();
    DoorEdge& door = doors[idx];

    const char* cause = nullptr;
    switch (kind) {
        case ActionKind::OpenDoor:
            if (door.state != DoorState::CLOSED) return false;
            door.state = DoorState::OPEN;
            cause = "open";
            break;
        case ActionKind::CloseDoor:
            if (door.state != DoorState::OPEN) return false;
            door.state = DoorState::CLOSED;
            cause = "close";
            break;
        case ActionKind::Breach:
            if (door.state != DoorState::CLOSED && door.state != DoorState::LOCKED)
                return false;
            door.state = DoorState::OPEN;
            cause = "breach";
            break;
        default:
            return false;
    }
    --ap;
    // Door state changes can alter room connectivity → recompute. O(W*H)
    // flood; acceptable at 16×12 and happens at most a few times per round.
    grid_.recompute_rooms();
    hooks.on_door_state_changed(round, door.a, door.b, door.state, cause);
    return true;
}

bool SimEngine::apply_deploy_drone(int round, RoundHooks& hooks,
                                   ScenarioEntity& actor, GridPos target) {
    if (actor.kind != EntityKind::Operator) return false;
    auto& ap = round_context_.operator_ap[actor.id];
    if (ap < 1) return false;
    // Target must be the operator's own cell or an adjacent passable FLOOR/COVER.
    if (!(target == actor.pos) && chebyshev_distance(actor.pos, target) != 1)
        return false;
    if (!grid_.passable(target)) return false;
    if (!(target == actor.pos) && !grid_.edge_passable(actor.pos, target)) return false;
    // Find team drone.
    ScenarioEntity* drone = nullptr;
    for (auto& e : entities_) {
        if (e.kind == EntityKind::Drone && e.team == actor.team && e.hp > 0) {
            drone = &e; break;
        }
    }
    if (!drone) return false;
    if (drone->drone_deployed) return false;
    // Don't stack on another alive entity.
    for (const auto& e : entities_)
        if (e.id != drone->id && e.hp > 0 && e.pos == target) return false;

    const GridPos from = drone->pos;
    drone->pos = target;
    drone->drone_deployed = true;
    --ap;
    hooks.on_unit_moved(round, drone->id, from, target, ap);
    return true;
}

bool SimEngine::step_activation(int round, RoundHooks& hooks,
                                const ActionRequest& req) {
    if (round != round_state_.round_number)
        throw std::logic_error("activation round mismatch");
    require_phase(RoundPhase::Activations);

    // Prime (or re-prime after a completed activation) to the next live actor.
    // Note: don't gate on `activation_state_.actor == 0` — entity ID 0 is a
    // valid actor (attacker_a in the canonical scenario). `started` is the
    // sole "is primed" indicator; it's reset to false at end-of-activation.
    if (!activation_state_.started)
        advance_to_next_activation();

    if (round_context_.activation_cursor >= round_context_.activation_order.size()) {
        hooks.on_phase_timing("activations_complete", 0.0);
        return true;
    }

    const size_t idx = round_context_.activation_order[round_context_.activation_cursor];
    ScenarioEntity& actor = entities_[idx];

    // Dispatch. Each handler is a silent no-op on failed validation.
    switch (req.kind) {
        case ActionKind::Move:
            apply_move(round, hooks, actor, req.target_cell); break;
        case ActionKind::Shoot:
            resolve_shot(round, hooks, actor, req.target_entity, /*overwatch_snap=*/false);
            break;
        case ActionKind::Overwatch:
            set_overwatch(round, hooks, actor); break;
        case ActionKind::Peek: {
            // Contract §4: 1 AP, reveals an adjacent cell. Step 2 scope:
            // spend AP and inject FRESH sightings for any enemy occupying
            // that cell (requires LOS, which for adjacency means the edge
            // is not blocked by a closed/locked door).
            if (chebyshev_distance(actor.pos, req.target_cell) != 1) break;
            if (!grid_.in_bounds(req.target_cell)) break;
            if (grid_.edge_blocks_los(actor.pos, req.target_cell)) break;
            auto& ap = round_context_.operator_ap[actor.id];
            if (ap < 1) break;
            --ap;
            for (const auto& e : entities_) {
                if (e.hp <= 0) continue;
                if (e.team == actor.team) continue;
                if (!(e.pos == req.target_cell)) continue;
                Sighting s{};
                s.observer = actor.id;
                s.target = e.id;
                s.estimated_position = e.pos;
                s.confidence = 1.0f;
                s.uncertainty = 0.0f;
                s.class_id = (e.kind == EntityKind::Drone) ? 2 : 1;
                beliefs_.apply_sighting(actor.team, s, round);
            }
            break;
        }
        case ActionKind::OpenDoor:
        case ActionKind::CloseDoor:
        case ActionKind::Breach:
            apply_door_action(round, hooks, actor, req.target_cell, req.kind);
            break;
        case ActionKind::DeployDrone:
            apply_deploy_drone(round, hooks, actor, req.target_cell); break;
        case ActionKind::Interact: {
            // Contract §4: 1 AP. Win-condition plumbing (office_breach
            // objective) is migration Step 6 — for now we only spend AP so
            // the action is well-defined and replay-logged via AP in world_hash.
            auto& ap = round_context_.operator_ap[actor.id];
            if (ap >= 1) --ap;
            break;
        }
        case ActionKind::HackDevice: {
            // Contract §4: 1 AP. Device-level cyber effects are migration
            // Step 4; here we just spend AP so the action is well-defined.
            auto& ap = round_context_.operator_ap[actor.id];
            if (ap >= 1) --ap;
            break;
        }
        case ActionKind::EndTurn:
            activation_state_.ended_turn = true;
            break;
    }

    // End-of-activation check: 0 AP left or explicit EndTurn.
    const auto ap_it = round_context_.operator_ap.find(actor.id);
    const int ap_remaining = (ap_it == round_context_.operator_ap.end()) ? 0 : ap_it->second;
    if (ap_remaining <= 0 || req.kind == ActionKind::EndTurn)
        activation_state_.ended_turn = true;

    if (activation_state_.ended_turn) {
        auto t0 = Clock::now();
        ++round_context_.activation_cursor;
        round_state_.activation_index = round_context_.activation_cursor;
        activation_state_ = ActivationState{};  // re-prime on next call
        hooks.on_phase_timing("activation", elapsed_us(t0));
    }

    return round_context_.activation_cursor >= round_context_.activation_order.size();
}

void SimEngine::finalize_round(int round, RoundHooks& hooks) {
    require_phase(RoundPhase::Activations);
    advance_phase(RoundPhase::RoundEnd);
    auto t0 = Clock::now();
    phase_round_end(round, hooks);
    hooks.on_phase_timing("round_end", elapsed_us(t0));
    advance_phase(RoundPhase::Idle);
}

void SimEngine::run_round(int round, RoundHooks& hooks) {
    begin_round(round, hooks);
    while (!step_activation(round, hooks, ActionRequest::end_turn())) {}
    finalize_round(round, hooks);
}
