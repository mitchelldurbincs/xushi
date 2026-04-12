#include "belief.h"
#include <algorithm>

// Uncertainty growth multiplier applied per negative-evidence observation.
// When a track is in sensor coverage but not detected, its uncertainty
// is scaled by this factor (10% growth) to reflect increased doubt.
static constexpr float kNegativeEvidenceUncertaintyGrowth = 1.1f;

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

void BeliefState::apply_negative_evidence(Vec2 observer_pos, float sensor_range,
                                           const Map& map,
                                           const std::vector<EntityId>& detected_targets,
                                           float factor) {
    for (auto& t : tracks) {
        // Skip targets that were actually detected this tick
        bool was_detected = false;
        for (EntityId id : detected_targets) {
            if (t.target == id) { was_detected = true; break; }
        }
        if (was_detected) continue;

        float dist = (t.estimated_position - observer_pos).length();
        if (dist > sensor_range) continue;
        if (!map.line_of_sight(observer_pos, t.estimated_position)) continue;

        // Track was in sensor coverage but not detected — reduce confidence
        t.confidence *= (1.0f - factor);
        t.uncertainty *= kNegativeEvidenceUncertaintyGrowth;
    }
}

const char* track_status_str(TrackStatus s) {
    switch (s) {
        case TrackStatus::FRESH: return "FRESH";
        case TrackStatus::STALE: return "STALE";
        case TrackStatus::EXPIRED: return "EXPIRED";
    }
    return "???";
}
