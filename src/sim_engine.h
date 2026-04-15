#pragma once

#include "scenario.h"
#include "constants.h"
#include "stats.h"
#include "belief.h"
#include "belief_state.h"
#include "truth_state.h"
#include "action.h"
#include "comm.h"
#include "movement.h"
#include "policy.h"
#include "map.h"
#include "rng.h"
#include "task.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game_mode.h"

// Hook interface for side effects. All methods are no-ops by default.
// Callers override only the hooks they care about.
struct TickHooks {
    virtual ~TickHooks() = default;

    // Movement
    virtual void on_entity_moved(int /*tick*/, EntityId /*id*/, Vec2 /*pos*/) {}
    virtual void on_waypoint_arrival(int /*tick*/, EntityId /*id*/, int /*waypoint_index*/, Vec2 /*pos*/) {}

    // Sensing
    virtual void on_detection(int /*tick*/, const Observation& /*obs*/) {}
    virtual void on_miss(int /*tick*/, EntityId /*sensor*/) {}
    virtual void on_false_positive(int /*tick*/, EntityId /*sensor*/, const Observation& /*phantom*/) {}

    // Communication
    virtual void on_msg_sent(int /*tick*/, EntityId /*sender*/, EntityId /*receiver*/, int /*delivery_tick*/) {}
    virtual void on_msg_dropped(int /*tick*/, EntityId /*sender*/, EntityId /*receiver*/) {}
    virtual void on_msg_delivered(int /*tick*/, EntityId /*sender*/, EntityId /*receiver*/) {}

    // Belief
    virtual void on_track_update(int /*tick*/, EntityId /*owner*/, const Track& /*trk*/) {}
    virtual void on_track_expired(int /*tick*/, EntityId /*owner*/, EntityId /*target*/) {}
    virtual void on_belief_invariant_check(const BeliefState& /*belief*/) {}

    // Actions
    virtual void on_action_resolved(int /*tick*/, const ActionResult& /*result*/) {}
    virtual void on_effect_resolved(int /*tick*/, const EffectOutcome& /*outcome*/) {}

    // Periodic snapshots
    virtual void on_world_hash(int /*tick*/, uint64_t /*hash*/) {}
    virtual void on_stats_snapshot(int /*tick*/, const SystemStats& /*stats*/) {}

    // Phase timing (called once per phase per tick)
    virtual void on_phase_timing(const char* /*phase*/, double /*elapsed_us*/) {}

    // Position invariant check (after movement)
    virtual void on_positions_check(const std::vector<ScenarioEntity>& /*entities*/) {}

    // Game mode
    virtual void on_game_mode_end(int /*tick*/, const GameModeResult& /*result*/) {}

    // Tasking
    virtual void on_task_assigned(int /*tick*/, const Task& /*task*/, const ScenarioEntity& /*assignee*/) {}
    virtual void on_task_completed(int /*tick*/, EntityId /*assignee*/, EntityId /*target*/, bool /*corroborated*/) {}
};

class SimEngine {
public:
    enum class RoundPhase {
        Idle,
        RoundStart,
        Cooldowns,
        Activations,
        SupportPublicationGate,
        Communication,
        Belief,
        ReactionResolution,
        Tasks,
        PeriodicSnapshots,
        RoundEnd
    };

    struct RoundState {
        int round_tick = -1;
        RoundPhase phase = RoundPhase::Idle;
        size_t activation_index = 0;
    };

    void init(const Scenario& scn, Policy* policy = nullptr,
              GameMode* game_mode = nullptr);

    // Legacy one-shot step API now delegates to explicit round phases.
    void step(int tick, TickHooks& hooks);

    // Round/activation authoritative stepping API.
    void begin_round(int tick, TickHooks& hooks);
    bool step_activation(int tick, TickHooks& hooks); // true when all activations done
    void finalize_round(int tick, TickHooks& hooks);

    const RoundState& round_state() const { return round_state_; }

    // Action queue — policies/controllers push requests, engine adjudicates
    void submit_action(const ActionRequest& req);
    const std::vector<DesignationRecord>& get_designations() const { return designations_; }

    // Accessors for result extraction
    const std::vector<ScenarioEntity>& get_entities() const { return entities_; }
    const std::map<EntityId, BeliefState>& get_beliefs() const { return beliefs_.states(); }
    const std::map<EntityId, Task>& get_active_tasks() const { return active_tasks_; }
    SystemStats& stats() { return stats_; }
    const SystemStats& stats() const { return stats_; }
    int tasks_assigned() const { return tasks_assigned_; }
    int tasks_completed() const { return tasks_completed_; }

    // World hash for determinism checking
    uint64_t compute_world_hash() const;

    // Game mode result from the most recent tick (default: not finished)
    bool has_game_mode() const { return game_mode_ != nullptr; }
    const GameModeResult& game_mode_result() const { return last_game_mode_result_; }

private:
    void tick_cooldowns();
    void tick_activation(int tick, size_t activation_index, TickHooks& hooks);
    void tick_communication(int tick, TickHooks& hooks, std::vector<Message>& delivered);
    void tick_belief(int tick, TickHooks& hooks, const std::vector<Message>& delivered);
    void tick_tasks(int tick, TickHooks& hooks);
    void tick_support_publication_gate(int tick, TickHooks& hooks);
    void tick_reaction_resolution(int tick, TickHooks& hooks);
    void tick_periodic_snapshots(int tick, TickHooks& hooks);
    void move_toward_target(ScenarioEntity& entity, const Vec2& target) const;

    void require_phase(RoundPhase expected) const;
    void advance_phase(RoundPhase next_phase);

    const Scenario* scn_ = nullptr;
    Map map_;
    std::vector<ScenarioEntity> entities_;
    std::vector<ScenarioEntity*> sensors_;
    std::vector<ScenarioEntity*> trackers_;
    std::vector<ScenarioEntity*> observables_;
    Rng rng_{0};
    CommSystem comms_;
    BeliefStateStore beliefs_;
    TruthState truth_state_;
    std::map<EntityId, Task> active_tasks_;
    int tasks_assigned_ = 0;
    int tasks_completed_ = 0;
    SystemStats stats_;
    NullPolicy null_policy_;
    Policy* policy_ = nullptr;
    // Game mode
    GameMode* game_mode_ = nullptr;
    GameModeResult last_game_mode_result_;

    // Action system
    std::vector<ActionRequest> pending_actions_;
    std::vector<DesignationRecord> designations_;
    uint64_t next_designation_id_ = 1;
    void adjudicate_actions_for_type(int tick, TickHooks& hooks, ActionType type);
    ScenarioEntity* find_entity(EntityId id);
    const Scenario::EffectProfile* find_effect_profile(uint32_t index) const;

    RoundState round_state_;
};
