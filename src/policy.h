#pragma once

#include "types.h"
#include "belief.h"
#include <optional>

// Bounded observation passed to policies. Contains only information the
// agent can actually know locally — no global state, no other agents' beliefs.
static constexpr int kMaxPolicyTracks = 8;

struct PolicyObservation {
    EntityId id;
    Vec2 position;
    int tick;

    // Agent's own local tracks (only if can_track), capped at kMaxPolicyTracks.
    // Sorted by confidence descending — highest-priority tracks first.
    Track local_tracks[kMaxPolicyTracks];
    int num_tracks = 0;
};

struct Policy {
    virtual ~Policy() = default;

    virtual std::optional<Vec2> get_move_target(const PolicyObservation& obs) = 0;
};

struct NullPolicy : Policy {
    std::optional<Vec2> get_move_target(const PolicyObservation&) override {
        return std::nullopt;
    }
};
