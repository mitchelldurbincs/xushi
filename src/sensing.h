#pragma once

#include "types.h"
#include "map.h"
#include "rng.h"

struct Observation {
    int tick;
    EntityId observer;
    EntityId target;
    Vec2 estimated_position;
    float uncertainty;   // noise radius (meters)
    float confidence;    // 0..1
};

// Attempt to sense a target. Returns true and fills `out` if LOS exists and
// target is within max_range. Position estimate includes gaussian noise that
// scales with distance. Confidence decreases with range.
bool sense(const Map& map,
           Vec2 observer_pos, EntityId observer_id,
           Vec2 target_pos, EntityId target_id,
           float max_range, int tick,
           Rng& rng, Observation& out);
