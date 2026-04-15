#include "comm.h"

#include <algorithm>

void CommSystem::add_jam(GridPos center, int radius, int rounds, int issued_by_team) {
    if (rounds <= 0) return;
    active_jams.push_back(ActiveJam{center, radius, rounds, issued_by_team});
}

void CommSystem::tick_down() {
    for (auto& j : active_jams)
        j.rounds_remaining--;
    active_jams.erase(
        std::remove_if(active_jams.begin(), active_jams.end(),
                       [](const ActiveJam& j) { return j.rounds_remaining <= 0; }),
        active_jams.end());
}

bool CommSystem::is_jammed(GridPos p) const {
    for (const auto& j : active_jams) {
        if (chebyshev_distance(j.center, p) <= j.radius)
            return true;
    }
    return false;
}
