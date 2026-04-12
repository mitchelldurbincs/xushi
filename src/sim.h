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

// Run a scenario headlessly and return results for comparison.
// No replay writing, no stdout output.
SimResult run_scenario_headless(const Scenario& scn);
