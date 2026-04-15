#include "sim_engine.h"

#include "constants.h"
#include "world_hash.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

double elapsed_us(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
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

bool SimEngine::step_activation(int round, RoundHooks& hooks) {
    if (round != round_state_.round_number)
        throw std::logic_error("activation round mismatch");
    require_phase(RoundPhase::Activations);

    if (round_context_.activation_cursor >= round_context_.activation_order.size()) {
        hooks.on_phase_timing("activations_complete", 0.0);
        return true;
    }

    // Placeholder activation: the operator does nothing (action system is
    // implemented in a subsequent migration step). Spend 0 AP. Emitting
    // deterministic per-activation bookkeeping keeps tests stable.
    auto t0 = Clock::now();
    const size_t idx = round_context_.activation_order[round_context_.activation_cursor++];
    (void)idx;
    round_state_.activation_index = round_context_.activation_cursor;
    hooks.on_phase_timing("activation", elapsed_us(t0));
    return false;
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
    while (!step_activation(round, hooks)) {}
    finalize_round(round, hooks);
}
