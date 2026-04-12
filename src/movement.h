#pragma once

#include "scenario.h"
#include <cmath>

struct WaypointEvent {
    bool arrived = false;
    int waypoint_index = -1;
};

// Arrival radius squared. Entity "arrives" at a waypoint when within this distance.
inline constexpr float kArrivalRadiusSq = 1.0f;

// Advance to next waypoint. Returns true if there is a next waypoint to move toward.
inline bool advance_waypoint(ScenarioEntity& e, WaypointEvent& event) {
    event.arrived = true;
    event.waypoint_index = e.current_waypoint;
    e.current_waypoint++;
    if (e.current_waypoint >= static_cast<int>(e.waypoints.size())) {
        if (e.waypoint_mode == ScenarioEntity::WaypointMode::Loop)
            e.current_waypoint = 0;
        else
            return false;  // Stop mode: no more waypoints
    }
    return true;
}

// Update a single entity's position for one tick.
// Entities with waypoints move toward the current waypoint at their configured speed.
// Entities without waypoints use constant velocity (existing behavior).
inline WaypointEvent update_movement(ScenarioEntity& e, float dt) {
    WaypointEvent event;

    if (e.waypoints.empty()) {
        e.position = e.position + e.velocity * dt;
        return event;
    }

    // Already finished (stop mode, past last waypoint)
    if (e.current_waypoint >= static_cast<int>(e.waypoints.size()))
        return event;

    Vec2 target = e.waypoints[e.current_waypoint];
    Vec2 diff = target - e.position;
    float dist_sq = diff.x * diff.x + diff.y * diff.y;

    // Already at the waypoint (within arrival radius)
    if (dist_sq <= kArrivalRadiusSq) {
        if (!advance_waypoint(e, event))
            return event;
        target = e.waypoints[e.current_waypoint];
        diff = target - e.position;
        dist_sq = diff.x * diff.x + diff.y * diff.y;
    }

    // Move toward target at constant speed
    float step = e.speed * dt;
    float dist = std::sqrt(dist_sq);

    if (dist <= 1e-9f || step >= dist) {
        // Overshoot or exact arrival: snap to waypoint
        e.position = target;
        if (!event.arrived)
            advance_waypoint(e, event);
    } else {
        e.position = e.position + diff * (step / dist);
    }

    return event;
}
