#pragma once

#include "policy.h"
#include "constants.h"
#include <map>
#include <vector>

// PatrolPolicy: cycles each agent through a list of patrol waypoints.
// When the agent is within arrival radius of the current patrol target,
// it advances to the next one (looping).
//
// This is independent of entity-level waypoints — it operates at the
// policy layer and can be overridden by tasks.
struct PatrolPolicy : Policy {
    struct Route {
        std::vector<Vec2> waypoints;
        int current = 0;
    };

    std::map<EntityId, Route> routes;

    std::optional<Vec2> get_move_target(const PolicyObservation& obs) override {
        auto it = routes.find(obs.id);
        if (it == routes.end()) return std::nullopt;

        Route& route = it->second;
        if (route.waypoints.empty()) return std::nullopt;

        Vec2 target = route.waypoints[route.current];

        // Check if arrived — advance to next waypoint
        Vec2 diff = target - obs.position;
        float dist_sq = diff.x * diff.x + diff.y * diff.y;
        if (dist_sq <= kTaskArrivalRadius * kTaskArrivalRadius) {
            route.current = (route.current + 1) % static_cast<int>(route.waypoints.size());
            target = route.waypoints[route.current];
        }

        return target;
    }
};
