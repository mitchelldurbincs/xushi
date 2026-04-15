#pragma once

#include "belief.h"

#include <map>

// Per-team belief store. Contract §8: belief is scoped to the team, not
// the unit. Indexed by team id (int). Unaffiliated trackers use team = -1.
class BeliefStateStore {
public:
    void clear() { beliefs_.clear(); }

    // Ensure a belief state exists for every given team id.
    void init_teams(const std::vector<int>& team_ids) {
        beliefs_.clear();
        for (int team : team_ids)
            beliefs_[team];
    }

    std::map<int, BeliefState>& states() { return beliefs_; }
    const std::map<int, BeliefState>& states() const { return beliefs_; }

    BeliefState* find(int team) {
        auto it = beliefs_.find(team);
        return it == beliefs_.end() ? nullptr : &it->second;
    }
    const BeliefState* find(int team) const {
        auto it = beliefs_.find(team);
        return it == beliefs_.end() ? nullptr : &it->second;
    }

    void apply_sighting(int team, const Sighting& s, int round) {
        auto* belief = find(team);
        if (belief)
            belief->update(s, round);
    }

private:
    std::map<int, BeliefState> beliefs_;
};
