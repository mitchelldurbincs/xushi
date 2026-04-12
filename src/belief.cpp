#include "belief.h"
#include <algorithm>

Track* BeliefState::find_track(EntityId target) {
    for (auto& t : tracks) {
        if (t.target == target) return &t;
    }
    return nullptr;
}

const Track* BeliefState::find_track(EntityId target) const {
    for (const auto& t : tracks) {
        if (t.target == target) return &t;
    }
    return nullptr;
}

void BeliefState::update(const Observation& obs, int current_tick) {
    Track* existing = find_track(obs.target);
    if (existing) {
        existing->estimated_position = obs.estimated_position;
        existing->confidence = obs.confidence;
        existing->uncertainty = obs.uncertainty;
        existing->last_update_tick = current_tick;
        existing->status = TrackStatus::FRESH;
    } else {
        tracks.push_back({
            obs.target,
            obs.estimated_position,
            obs.confidence,
            obs.uncertainty,
            current_tick,
            TrackStatus::FRESH
        });
    }
}

void BeliefState::decay(int current_tick, const BeliefConfig& config) {
    for (auto& t : tracks) {
        int age = current_tick - t.last_update_tick;

        if (age <= config.fresh_ticks) {
            t.status = TrackStatus::FRESH;
        } else if (age <= config.fresh_ticks + config.stale_ticks) {
            t.status = TrackStatus::STALE;
            // Decay confidence and grow uncertainty for stale tracks
            t.confidence = std::max(0.0f, t.confidence - config.confidence_decay_rate);
            t.uncertainty += config.uncertainty_growth_rate;
        } else {
            t.status = TrackStatus::EXPIRED;
        }
    }

    // Remove expired tracks
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
            [](const Track& t) { return t.status == TrackStatus::EXPIRED; }),
        tracks.end());
}

const char* track_status_str(TrackStatus s) {
    switch (s) {
        case TrackStatus::FRESH: return "FRESH";
        case TrackStatus::STALE: return "STALE";
        case TrackStatus::EXPIRED: return "EXPIRED";
    }
    return "???";
}
