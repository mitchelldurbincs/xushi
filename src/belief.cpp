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
        existing->last_decay_tick = current_tick;
        existing->status = TrackStatus::FRESH;
    } else {
        tracks.push_back({
            obs.target,
            obs.estimated_position,
            obs.confidence,
            obs.uncertainty,
            current_tick,
            current_tick,
            TrackStatus::FRESH
        });
    }
}

void BeliefState::decay(int current_tick, float dt, const BeliefConfig& config) {
    for (auto& t : tracks) {
        int age = current_tick - t.last_update_tick;
        int prev_age = t.last_decay_tick - t.last_update_tick;
        if (current_tick < t.last_decay_tick)
            prev_age = age;

        if (age <= config.fresh_ticks) {
            t.status = TrackStatus::FRESH;
        } else if (age <= config.fresh_ticks + config.stale_ticks) {
            t.status = TrackStatus::STALE;
            int stale_start = config.fresh_ticks;
            int stale_end = config.fresh_ticks + config.stale_ticks;
            int stale_ticks_elapsed =
                std::max(0, std::min(age, stale_end) - std::max(prev_age, stale_start));
            float stale_seconds_elapsed = static_cast<float>(stale_ticks_elapsed) * dt;
            if (stale_seconds_elapsed > 0.0f) {
                t.confidence = std::max(
                    0.0f,
                    t.confidence - config.confidence_decay_per_second * stale_seconds_elapsed);
                t.uncertainty +=
                    config.uncertainty_growth_per_second * stale_seconds_elapsed;
            }
        } else {
            t.status = TrackStatus::EXPIRED;
        }
        t.last_decay_tick = current_tick;
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
