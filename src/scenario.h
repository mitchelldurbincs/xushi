#pragma once

#include "types.h"
#include "map.h"
#include "comm.h"
#include "belief.h"
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

    // Waypoint movement (optional; empty = use constant velocity)
    std::vector<Vec2> waypoints;
    float speed = 0.0f;
    WaypointMode waypoint_mode = WaypointMode::Stop;
    int current_waypoint = 0;  // runtime state: index into waypoints
};

struct Scenario {
    uint64_t seed = 0;
    float dt = 1.0f;
    int ticks = 100;
    float max_sensor_range = 80.0f;

    std::vector<Rect> obstacles;
    std::vector<ScenarioEntity> entities;

    CommChannel channel = {3, 0.0f, 0.1f};
    BeliefConfig belief = {};
};

// Load a scenario from a JSON file. Throws std::runtime_error on failure.
Scenario load_scenario(const std::string& path);
