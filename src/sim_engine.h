#pragma once

#include "scenario.h"
#include "constants.h"
#include "stats.h"
#include "belief.h"
#include "comm.h"
#include "movement.h"
#include "policy.h"
#include "task.h"
#include "map.h"
#include "rng.h"
#include <cstdint>
#include <map>
#include <vector>

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

    // Tasks
    virtual void on_task_assigned(int /*tick*/, const Task& /*task*/, const ScenarioEntity& /*entity*/) {}
    virtual void on_task_completed(int /*tick*/, EntityId /*entity*/, EntityId /*target*/, bool /*corroborated*/) {}

    // Periodic snapshots
    virtual void on_world_hash(int /*tick*/, uint64_t /*hash*/) {}
    virtual void on_stats_snapshot(int /*tick*/, const SystemStats& /*stats*/) {}

    // Phase timing (called once per phase per tick)
    virtual void on_phase_timing(const char* /*phase*/, double /*elapsed_us*/) {}

    // Position invariant check (after movement)
    virtual void on_positions_check(const std::vector<ScenarioEntity>& /*entities*/) {}
};

// Shared simulation engine. Owns all mutable tick state.
// Both headless and CLI paths call init() then step() in a loop.
class SimEngine {
public:
    void init(const Scenario& scn, Policy* policy = nullptr);
    void step(int tick, TickHooks& hooks);

    // Accessors for result extraction
    const std::vector<ScenarioEntity>& get_entities() const { return entities_; }
    const std::map<EntityId, BeliefState>& get_beliefs() const { return beliefs_; }
    const std::map<EntityId, Task>& get_active_tasks() const { return active_tasks_; }
    SystemStats& stats() { return stats_; }
    const SystemStats& stats() const { return stats_; }
    int tasks_assigned() const { return tasks_assigned_; }
    int tasks_completed() const { return tasks_completed_; }

    // World hash for determinism checking
    uint64_t compute_world_hash() const;

private:
    const Scenario* scn_ = nullptr;
    Map map_;
    std::vector<ScenarioEntity> entities_;
    std::vector<ScenarioEntity*> sensors_;
    std::vector<ScenarioEntity*> trackers_;
    std::vector<ScenarioEntity*> observables_;
    Rng rng_{0};
    CommSystem comms_;
    std::map<EntityId, BeliefState> beliefs_;
    SystemStats stats_;
    NullPolicy null_policy_;
    Policy* policy_ = nullptr;
    std::map<EntityId, Task> active_tasks_;
    int tasks_assigned_ = 0;
    int tasks_completed_ = 0;

};
