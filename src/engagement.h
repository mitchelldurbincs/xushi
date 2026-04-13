#pragma once

#include "action.h"
#include "belief.h"
#include "map.h"
#include "scenario.h"
#include <optional>

struct EngagementWorldState {
    const Map* map = nullptr;
    int tick = 0;
    const std::vector<ScenarioEntity>* entities = nullptr;
    EntityId actor_id = kInvalidEntity;
    EntityId track_target_id = kInvalidEntity;
};

struct EngagementGateInputs {
    const ScenarioEntity* actor = nullptr;
    const Track* target_track = nullptr;
    const ScenarioEntity* target_truth = nullptr;
    uint32_t effect_profile_index = 0;
    EngagementWorldState world;
};

struct EngagementGateDebug {
    std::optional<int> track_age_ticks;
    std::optional<float> track_uncertainty;
    std::optional<float> track_identity_confidence;
    std::optional<int> track_corroboration_count;
    std::optional<float> actor_to_truth_distance;
    std::optional<bool> has_line_of_effect;
};

struct EngagementGateResult {
    uint32_t failure_mask = 0;
    EngagementGateDebug debug;

    bool allowed() const { return failure_mask == 0; }
};

EngagementGateResult compute_engagement_gates(const EngagementGateInputs& in);
