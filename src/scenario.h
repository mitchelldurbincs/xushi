#pragma once

#include "types.h"
#include "map.h"
#include "comm.h"
#include "belief.h"
#include <map>
#include <string>
#include <vector>

struct ScenarioEntity {
    enum class WaypointMode { Stop, Loop };

    EntityId id;
    std::string role_name;
    Vec2 position;
    Vec2 velocity;

    // Capability flags (drive sim behavior instead of role enum)
    bool can_sense = false;      // runs sensing pass (observer)
    bool can_track = false;      // maintains BeliefState (receiver)
    bool is_observable = false;  // can be sensed by sensors
    bool can_engage = false;     // can execute EngageTrack actions

    int team = -1;              // team/faction id (-1 = unaffiliated)
    int class_id = 0;           // truth class for identity/classification
    int vitality = 100;         // current health/vitality points
    int max_vitality = 100;
    int ammo = 0;               // ammo remaining (runtime state)
    int cooldown_ticks_remaining = 0;
    std::vector<int> allowed_effect_profile_indices;

    // Waypoint movement (optional; empty = use constant velocity)
    std::vector<Vec2> waypoints;
    float speed = 0.0f;
    WaypointMode waypoint_mode = WaypointMode::Stop;
    int current_waypoint = 0;  // runtime state: index into waypoints
    std::map<int, std::vector<int>> branch_points;  // waypoint_idx -> successor indices
};

struct Scenario {
    struct EngagementRulesConfig {
        Vec2 protected_zone_center = {0.0f, 0.0f};
        float protected_zone_radius = 10.0f;
        float friendly_risk_radius = 8.0f;
        float default_effect_range = 80.0f;
        float effect_range_step = 20.0f;
        float max_track_uncertainty = 20.0f;
        float min_identity_confidence = 0.5f;
        int min_corroboration_count = 1;
    };

    // Unified EffectProfile supporting both tactical gates and outcome resolution
    struct EffectProfile {
        // Identification
        std::string name;

        // Engagement gates (tactical)
        float range = 0.0f;
        bool requires_los = false;
        float identity_threshold = 0.0f;
        float corroboration_threshold = 0.0f;

        // Outcome resolution (effect simulation)
        float hit_probability = 1.0f;
        int vitality_delta_min = 0;  // inclusive signed bounds (negative = damage)
        int vitality_delta_max = 0;

        // Resource costs
        int cooldown_ticks = 0;
        int ammo_cost = 0;

        std::vector<std::string> roe_flags;  // optional
    };

    uint64_t seed = 0;
    float dt = 1.0f;
    int ticks = 100;
    float max_sensor_range = 80.0f;

    std::vector<Rect> obstacles;
    std::vector<ScenarioEntity> entities;
    std::vector<EffectProfile> effect_profiles;

    CommChannel channel = {3, 0.0f, 0.1f};
    BeliefConfig belief = {};

    struct PerceptionConfig {
        float miss_rate = 0.0f;            // probability of missing a valid detection [0,1]
        float false_positive_rate = 0.0f;  // probability of phantom detection per sensor per tick
        float class_confusion_rate = 0.0f; // probability of misidentifying target class
    };
    PerceptionConfig perception = {};
    EngagementRulesConfig engagement_rules = {};

    // Policy configuration (optional). Only "patrol" type supported.
    struct PolicyConfig {
        std::string type;  // empty = no policy, "patrol" = PatrolPolicy
        // For patrol: maps entity ID -> list of patrol waypoints
        std::map<EntityId, std::vector<Vec2>> patrol_routes;
    };
    PolicyConfig policy_config = {};

    // Game mode configuration (optional). Empty type = no game mode.
    struct GameModeConfig {
        std::string type;  // "asset_protection", etc.
        std::vector<int> asset_entity_ids;  // for asset_protection mode
    };
    GameModeConfig game_mode_config = {};
};

// Load a scenario from a JSON file. Throws std::runtime_error on failure.
Scenario load_scenario(const std::string& path);
