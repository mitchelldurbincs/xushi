#include "sim.h"
#include "sim_engine.h"
#include "patrol_policy.h"
#include "world_hash.h"
#include <memory>

uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                            const std::map<EntityId, BeliefState>& beliefs) {
    return compute_world_hash_canonical(entities, beliefs);
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

    for (int tick = 0; tick < scn.ticks; ++tick) {
        engine.begin_round(tick, hooks);
        while (!engine.step_activation(tick, hooks)) {
        }
        engine.finalize_round(tick, hooks);

        if (engine.has_game_mode() && engine.game_mode_result().finished)
            break;
    }

    result.stats = engine.stats();
    result.final_track_count = 0;
    for (const auto& [gid, belief] : engine.get_beliefs())
        result.final_track_count += static_cast<int>(belief.tracks.size());
    return result;
}
