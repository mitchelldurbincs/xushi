#pragma once

#include "grid.h"
#include "types.h"

#include <vector>

// Binary jam model (contract §9). A jam is a circular region of effect
// centered on a cell with an integer cell radius and a round duration.
// The engine ticks durations down at round start. A unit at a cell that
// lies within any active jam is "jammed" and its sightings do not
// propagate to team belief that round.
struct ActiveJam {
    GridPos center{};
    int radius = 0;         // Chebyshev cells
    int rounds_remaining = 0;
    int team_issued_by = -1; // informational; a team cannot jam itself
};

struct CommSystem {
    std::vector<ActiveJam> active_jams;

    void clear() { active_jams.clear(); }

    // Add a new jam (contract §6 "cyber jam": 3-cell radius, 1 round).
    void add_jam(GridPos center, int radius, int rounds, int issued_by_team);

    // Decrement all durations; remove expired jams. Called at Phase 1
    // (start of round).
    void tick_down();

    // Is the given cell covered by any active jam?
    bool is_jammed(GridPos p) const;
};
