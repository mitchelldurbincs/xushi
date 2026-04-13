#pragma once

#include "belief.h"
#include "scenario.h"
#include <cstdint>
#include <map>
#include <vector>

// Canonical FNV-1a world hash used by both headless and direct-engine paths.
// Keep entity/track field order in lockstep with world_hash.cpp.
uint64_t compute_world_hash_canonical(const std::vector<ScenarioEntity>& entities,
                                      const std::map<EntityId, BeliefState>& beliefs);
