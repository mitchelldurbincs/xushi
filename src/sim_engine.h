#pragma once

#include "action.h"
#include "belief.h"
#include "belief_state.h"
#include "comm.h"
#include "game_mode.h"
#include "grid.h"
#include "replay_events.h"
#include "rng.h"
#include "scenario.h"
#include "types.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // Action resolution (contract §4, §7). Step 2 scope: Move, Shoot (with
    // damage), Overwatch set, and door state changes. Overwatch *triggers*
    // land in migration Step 3.
    virtual void on_unit_moved(int /*round*/, EntityId /*actor*/,
                               GridPos /*from*/, GridPos /*to*/,
                               int /*ap_after*/) {}
    virtual void on_shot_resolved(int /*round*/, EntityId /*shooter*/,
                                  EntityId /*target*/,
                                  const ShotModifiers& /*m*/) {}
    virtual void on_damage(int /*round*/, EntityId /*shooter*/, EntityId /*target*/,
                           int /*damage*/, int /*hp_after*/, bool /*eliminated*/) {}
    virtual void on_overwatch_set(int /*round*/, EntityId /*actor*/) {}
    virtual void on_door_state_changed(int /*round*/, GridPos /*a*/, GridPos /*b*/,
                                       DoorState /*new_state*/, const char* /*cause*/) {}

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
        std::unordered_set<EntityId> moved_this_round;  // contract §4 modifier
    };

    // Per-activation scratch. Reset when the cursor advances to a new operator.
    struct ActivationState {
        EntityId actor = 0;
        bool started = false;             // true once the first action runs
        bool shot_this_activation = false;
        bool ended_turn = false;
    };

    void init(const Scenario& scn, GameMode* game_mode = nullptr);

    // Drive one full round (all phases). Every activation is silently ended
    // (no actions taken) — this is the default driver used by the CLI and
    // by phase-ordering tests. Tests exercising the action system should use
    // begin_round / step_activation / finalize_round directly.
    void run_round(int round, RoundHooks& hooks);

    // Phase-level API for tests / fine-grained control.
    void begin_round(int round, RoundHooks& hooks);

    // True iff there is still an operator to activate AND that operator has
    // not yet ended its turn. When false, the engine is either between
    // activations (cursor advanced internally) or past the last one; call
    // step_activation to progress.
    bool activation_needs_action() const;

    // Submit an action for the current operator. Returns true when *all*
    // activations for the round are complete. Silently no-ops on invalid
    // requests (fails pre-conditions like AP, LOS, adjacency).
    bool step_activation(int round, RoundHooks& hooks, const ActionRequest& req);

    void finalize_round(int round, RoundHooks& hooks);

    const RoundState& round_state() const { return round_state_; }
    const RoundContext& round_context() const { return round_context_; }

    // Accessors.
    const std::vector<ScenarioEntity>& get_entities() const { return entities_; }
    std::vector<ScenarioEntity>& entities() { return entities_; }
    const std::map<int, BeliefState>& get_beliefs() const { return beliefs_.states(); }
    BeliefStateStore& beliefs() { return beliefs_; }
    const GridMap& grid() const { return grid_; }
    const CommSystem& comms() const { return comms_; }

    ScenarioEntity* find_entity(EntityId id);
    const ScenarioEntity* find_entity(EntityId id) const;

    uint64_t compute_world_hash() const;

    bool has_game_mode() const { return game_mode_ != nullptr; }
    const GameModeResult& game_mode_result() const { return last_game_mode_result_; }

    // Exposed for isolated unit tests of the §4 hit formula. Computes the
    // hit probability (in [0.05, 0.95]) for `shooter` firing at `target`
    // given current belief + grid + round state. `overwatch_snap` = true
    // models the -15% snap-fire penalty (triggered fires land in Step 3).
    float compute_hit_probability(const ScenarioEntity& shooter,
                                  const ScenarioEntity& target,
                                  bool overwatch_snap) const;

private:
    void reset_round_context(int round);
    void build_activation_order_for_round();

    void phase_round_start();
    void phase_support();
    void phase_round_end(int round, RoundHooks& hooks);

    void require_phase(RoundPhase expected) const;
    void advance_phase(RoundPhase p) { round_state_.phase = p; }

    // Advances the activation cursor past dead or fully-spent operators.
    // Sets activation_state_.actor to the next unit that still needs input,
    // or leaves cursor == activation_order.size() when all are done.
    void advance_to_next_activation();

    // Action handlers. Each returns true if the action was successfully
    // applied (AP/resource cost was taken, state mutated); false on a silent
    // no-op (failed validation). Hook emissions happen inside the handlers.
    bool apply_move(int round, RoundHooks& hooks,
                    ScenarioEntity& actor, GridPos target);
    bool resolve_shot(int round, RoundHooks& hooks,
                      ScenarioEntity& shooter, EntityId target_id,
                      bool overwatch_snap);
    bool set_overwatch(int round, RoundHooks& hooks, ScenarioEntity& actor);
    bool apply_door_action(int round, RoundHooks& hooks,
                           ScenarioEntity& actor, GridPos target,
                           ActionKind kind);
    bool apply_deploy_drone(int round, RoundHooks& hooks,
                            ScenarioEntity& actor, GridPos target);

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
    ActivationState activation_state_;
};
