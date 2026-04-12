#pragma once

#include "types.h"
#include "sensing.h"
#include <vector>

enum class TrackStatus { FRESH, STALE, EXPIRED };

struct BeliefConfig {
    int fresh_ticks = 5;
    int stale_ticks = 10;               // ticks after FRESH before EXPIRED
    float uncertainty_growth_per_second = 0.5f; // meters / second while STALE
    float confidence_decay_per_second = 0.05f;  // confidence units / second while STALE
};

struct Track {
    EntityId target;
    Vec2 estimated_position;
    float confidence;
    float uncertainty;
    int last_update_tick;
    int last_decay_tick;
    TrackStatus status;
};

struct BeliefState {
    std::vector<Track> tracks;

    // Create or refresh a track from an observation.
    void update(const Observation& obs, int current_tick);

    // Age all tracks: grow uncertainty, decay confidence, transition status,
    // remove expired tracks. Decay/growth rates are scaled by elapsed
    // simulation time (seconds), using dt.
    void decay(int current_tick, float dt, const BeliefConfig& config);

    // Find a track for a given target, or nullptr if none.
    Track* find_track(EntityId target);
    const Track* find_track(EntityId target) const;
};

const char* track_status_str(TrackStatus s);
