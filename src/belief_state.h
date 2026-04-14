#pragma once

#include "belief.h"
#include "scenario.h"
#include "observation_state.h"
#include <map>

class BeliefStateStore {
public:
    void clear() { beliefs_.clear(); }

    void init_trackers(const std::vector<ScenarioEntity*>& trackers) {
        beliefs_.clear();
        for (auto* t : trackers)
            beliefs_[t->id] = BeliefState{};
    }

    std::map<EntityId, BeliefState>& states() { return beliefs_; }
    const std::map<EntityId, BeliefState>& states() const { return beliefs_; }

    BeliefState* find(EntityId owner) {
        auto it = beliefs_.find(owner);
        if (it == beliefs_.end()) return nullptr;
        return &it->second;
    }

    const BeliefState* find(EntityId owner) const {
        auto it = beliefs_.find(owner);
        if (it == beliefs_.end()) return nullptr;
        return &it->second;
    }

    void apply_local_observation(EntityId owner, const Observation& obs, int tick) {
        auto* belief = find(owner);
        if (belief)
            belief->update(obs, tick);
    }

    void apply_published_observation(EntityId owner, const SharedObservationPayload& payload, int tick) {
        auto* belief = find(owner);
        if (belief)
            belief->update(to_observation(payload), tick);
    }

    void apply_negative_evidence(EntityId owner,
                                 Vec2 observer_pos,
                                 float sensor_range,
                                 const Map& map,
                                 const std::vector<EntityId>& detected_targets,
                                 float factor) {
        auto* belief = find(owner);
        if (belief) {
            belief->apply_negative_evidence(observer_pos, sensor_range, map,
                                            detected_targets, factor);
        }
    }

private:
    std::map<EntityId, BeliefState> beliefs_;
};
