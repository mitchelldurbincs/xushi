#pragma once

#include "scenario.h"
#include "stats.h"
#include <cstdint>
#include <map>
#include <vector>

struct SimResult {
    std::vector<uint64_t> world_hashes; // one per hash interval
    SystemStats stats;
    int final_track_count;
    int tasks_assigned = 0;
    int tasks_completed = 0;
};

// FNV-1a hash of entity positions and belief state. Used for determinism checks.
uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                            const std::map<EntityId, BeliefState>& beliefs);

// Run a scenario headlessly and return results for comparison.
// No replay writing, no stdout output.
SimResult run_scenario_headless(const Scenario& scn);
