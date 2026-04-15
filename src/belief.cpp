#include "belief.h"

#include <algorithm>

Track* BeliefState::find_track(EntityId target) {
    for (auto& t : tracks)
        if (t.target == target) return &t;
    return nullptr;
}

const Track* BeliefState::find_track(EntityId target) const {
    for (const auto& t : tracks)
        if (t.target == target) return &t;
    return nullptr;
}

void BeliefState::update(const Sighting& s, int current_round) {
    Track* existing = find_track(s.target);
    uint32_t src_bit = s.is_spoof ? 0u : (1u << (s.observer % 32));
    if (existing) {
        existing->estimated_position = s.estimated_position;
        existing->confidence = s.confidence;
        existing->uncertainty = s.uncertainty;
        existing->last_update_round = current_round;
        existing->last_decay_round = current_round;
        existing->status = TrackStatus::FRESH;
        if (s.class_id != 0)
            existing->class_id = s.class_id;
        existing->source_mask |= src_bit;
        return;
    }
    Track t{};
    t.target = s.target;
    t.estimated_position = s.estimated_position;
    t.confidence = s.confidence;
    t.uncertainty = s.uncertainty;
    t.last_update_round = current_round;
    t.last_decay_round = current_round;
    t.status = TrackStatus::FRESH;
    t.class_id = s.class_id;
    t.source_mask = src_bit;
    tracks.push_back(t);
}

void BeliefState::decay(int current_round, const BeliefConfig& config) {
    for (auto& t : tracks) {
        int age = current_round - t.last_update_round;
        int prev_age = t.last_decay_round - t.last_update_round;
        if (current_round < t.last_decay_round)
            prev_age = age;

        if (age <= config.fresh_rounds) {
            t.status = TrackStatus::FRESH;
        } else if (age <= config.fresh_rounds + config.stale_rounds) {
            t.status = TrackStatus::STALE;
            const int stale_start = config.fresh_rounds;
            const int stale_end = config.fresh_rounds + config.stale_rounds;
            int stale_rounds_elapsed =
                std::max(0, std::min(age, stale_end) - std::max(prev_age, stale_start));
            if (stale_rounds_elapsed > 0) {
                const float k = static_cast<float>(stale_rounds_elapsed);
                t.confidence = std::max(0.0f, t.confidence - config.confidence_decay_per_round * k);
                t.uncertainty += config.uncertainty_growth_per_round * k;
            }
        } else {
            t.status = TrackStatus::EXPIRED;
        }
        t.last_decay_round = current_round;
    }
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [](const Track& t) { return t.status == TrackStatus::EXPIRED; }),
        tracks.end());
}

void BeliefState::clear_spoofs_in(GridPos center, int radius) {
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [&](const Track& t) {
                           return t.source_mask == 0u &&
                                  chebyshev_distance(t.estimated_position, center) <= radius;
                       }),
        tracks.end());
}

const char* track_status_str(TrackStatus s) {
    switch (s) {
        case TrackStatus::FRESH:   return "FRESH";
        case TrackStatus::STALE:   return "STALE";
        case TrackStatus::EXPIRED: return "EXPIRED";
    }
    return "???";
}
