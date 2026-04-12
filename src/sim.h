#pragma once

#include "scenario.h"
#include "stats.h"
#include <cstdint>
#include <vector>

struct SimResult {
    std::vector<uint64_t> world_hashes; // one per hash interval
    SystemStats stats;
    int final_track_count;
};

// FNV-1a hash of entity positions and belief state. Used for determinism checks.
uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                            const BeliefState& belief);

// Run a scenario headlessly and return results for comparison.
// No replay writing, no stdout output.
SimResult run_scenario_headless(const Scenario& scn);
