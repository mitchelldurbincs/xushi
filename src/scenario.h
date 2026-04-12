#pragma once

#include "types.h"
#include "map.h"
#include "comm.h"
#include "belief.h"
#include <string>
#include <vector>

struct ScenarioEntity {
    EntityId id;
    std::string type; // "drone", "ground", "target"
    Vec2 position;
    Vec2 velocity;
};

struct Scenario {
    uint64_t seed = 0;
    float dt = 1.0f;
    int ticks = 100;
    float max_sensor_range = 80.0f;

    std::vector<Rect> obstacles;
    std::vector<ScenarioEntity> entities;

    CommChannel channel = {3, 0.0f, 0.1f};
    BeliefConfig belief = {};
};

// Load a scenario from a JSON file. Throws std::runtime_error on failure.
Scenario load_scenario(const std::string& path);
