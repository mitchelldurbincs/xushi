#pragma once

#include "belief.h"
#include "belief_state.h"
#include "comm.h"
#include "game_mode.h"
#include "grid.h"
#include "rng.h"
#include "scenario.h"
#include "types.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// Hook interface for side effects (e.g. replay, logging, stats). All methods
// are no-ops by default. Callers override only the hooks they care about.
struct RoundHooks {
    virtual ~RoundHooks() = default;

    // Phase timing: called after each phase within a round.
    virtual void on_phase_timing(const char* /*phase*/, double /*elapsed_us*/) {}

    // Round boundaries.
    virtual void on_round_started(int /*round*/, int /*initiative_team*/) {}
    virtual void on_round_ended(int /*round*/) {}

    // Belief / tracks.
    virtual void on_track_update(int /*round*/, int /*team*/, const Track& /*trk*/) {}
    virtual void on_track_expired(int /*round*/, int /*team*/, EntityId /*target*/) {}

    // Determinism / game mode.
    virtual void on_world_hash(int /*round*/, uint64_t /*hash*/) {}
    virtual void on_game_mode_end(int /*round*/, const GameModeResult& /*result*/) {}
};

// Per-round state used by the phase driver (contract §2).
class SimEngine {
public:
    enum class RoundPhase {
        Idle,
        RoundStart,
        SupportPhase,
        Activations,
        RoundEnd,
    };

    struct RoundState {
        int round_number = -1;
        int initiative_team = -1;
        RoundPhase phase = RoundPhase::Idle;
        size_t activation_index = 0;
    };

    struct RoundContext {
        int round_number = -1;
        int initiative_team = -1;
        size_t activation_cursor = 0;
        std::vector<size_t> activation_order;  // indices into entities_
        std::unordered_map<EntityId, int> operator_ap;
        std::unordered_map<EntityId, int> operator_ap_max;
        std::unordered_map<int, int> support_ap;
        std::unordered_map<int, int> support_ap_max;
        std::unordered_map<int, int> support_ap_spent;
    };

    void init(const Scenario& scn, GameMode* game_mode = nullptr);

    // Drive one full round (all phases). The caller should invoke this
    // repeatedly until game_mode_result().finished or until
    // round_state().round_number + 1 >= scenario rounds.
    void run_round(int round, RoundHooks& hooks);

    // Phase-level API for tests / fine-grained control.
    void begin_round(int round, RoundHooks& hooks);
    bool step_activation(int round, RoundHooks& hooks);  // true when all activations done
    void finalize_round(int round, RoundHooks& hooks);

    const RoundState& round_state() const { return round_state_; }
    const RoundContext& round_context() const { return round_context_; }

    // Accessors.
    const std::vector<ScenarioEntity>& get_entities() const { return entities_; }
    std::vector<ScenarioEntity>& entities() { return entities_; }
    const std::map<int, BeliefState>& get_beliefs() const { return beliefs_.states(); }
    const GridMap& grid() const { return grid_; }
    const CommSystem& comms() const { return comms_; }

    ScenarioEntity* find_entity(EntityId id);
    const ScenarioEntity* find_entity(EntityId id) const;

    uint64_t compute_world_hash() const;

    bool has_game_mode() const { return game_mode_ != nullptr; }
    const GameModeResult& game_mode_result() const { return last_game_mode_result_; }

private:
    void reset_round_context(int round);
    void build_activation_order_for_round();

    void phase_round_start();
    void phase_support();
    void phase_round_end(int round, RoundHooks& hooks);

    void require_phase(RoundPhase expected) const;
    void advance_phase(RoundPhase p) { round_state_.phase = p; }

    const Scenario* scn_ = nullptr;
    GridMap grid_;
    std::vector<ScenarioEntity> entities_;
    std::vector<int> team_ids_;  // deterministic sorted unique set

    Rng rng_{0};
    CommSystem comms_;
    BeliefStateStore beliefs_;
    BeliefConfig belief_config_{};

    GameMode* game_mode_ = nullptr;
    GameModeResult last_game_mode_result_;

    RoundState round_state_;
    RoundContext round_context_;
};
