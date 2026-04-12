#include "sim.h"
#include "sim_engine.h"
#include "patrol_policy.h"
#include <memory>

uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                            const std::map<EntityId, BeliefState>& beliefs) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };
    for (const auto& e : entities) {
        mix(&e.id, sizeof(e.id));
        mix(&e.position.x, sizeof(float));
        mix(&e.position.y, sizeof(float));
        mix(&e.current_waypoint, sizeof(e.current_waypoint));
    }
    for (const auto& [owner_id, belief] : beliefs) {
        mix(&owner_id, sizeof(owner_id));
        for (const auto& t : belief.tracks) {
            mix(&t.target, sizeof(t.target));
            mix(&t.estimated_position.x, sizeof(float));
            mix(&t.estimated_position.y, sizeof(float));
            mix(&t.confidence, sizeof(float));
            mix(&t.uncertainty, sizeof(float));
        }
    }
    return h;
}

struct HeadlessHooks : TickHooks {
    SimResult* result = nullptr;

    void on_world_hash(int /*tick*/, uint64_t hash) override {
        result->world_hashes.push_back(hash);
    }
};

SimResult run_scenario_headless(const Scenario& scn) {
    SimResult result{};

    std::unique_ptr<Policy> policy;
    if (scn.policy_config.type == "patrol") {
        auto pp = std::make_unique<PatrolPolicy>();
        for (const auto& [eid, wps] : scn.policy_config.patrol_routes)
            pp->routes[eid] = {wps, 0};
        policy = std::move(pp);
    }

    SimEngine engine;
    engine.init(scn, policy.get());

    bool has_sensors = false, has_trackers = false, has_observables = false;
    for (const auto& e : scn.entities) {
        if (e.can_sense)     has_sensors = true;
        if (e.can_track)     has_trackers = true;
        if (e.is_observable) has_observables = true;
    }
    if (!has_sensors || !has_trackers || !has_observables)
        return result;

    HeadlessHooks hooks;
    hooks.result = &result;

    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    result.stats = engine.stats();
    result.tasks_assigned = engine.tasks_assigned();
    result.tasks_completed = engine.tasks_completed();
    result.final_track_count = 0;
    for (const auto& [gid, belief] : engine.get_beliefs())
        result.final_track_count += static_cast<int>(belief.tracks.size());
    return result;
}
