#pragma once

#include "scenario.h"
#include <memory>
#include <string>
#include <vector>

struct GameModeResult {
    bool finished = false;
    int winning_team = -1;  // -1 = draw / time-out
    std::string reason;
};

// Abstract interface for game modes.
// SimEngine calls these hooks at well-defined points during the tick loop.
// Implement this interface to define custom win/loss conditions and scoring.
struct GameMode {
    virtual ~GameMode() = default;

    // Called once after SimEngine::init().
    virtual void init(const Scenario& scn, const std::vector<ScenarioEntity>& entities) = 0;

    // Called at the start of each tick, before any simulation phases.
    virtual void on_tick_start(int /*tick*/, const std::vector<ScenarioEntity>& /*entities*/) {}

    // Called when an entity's vitality changes from effect resolution.
    virtual void on_entity_damaged(int /*tick*/, EntityId /*target*/,
                                   int /*vitality_before*/, int /*vitality_after*/) {}

    // Called after all tick phases complete.
    // Return result with finished=true to end the simulation early.
    virtual GameModeResult on_tick_end(int tick,
                                       const std::vector<ScenarioEntity>& entities) = 0;
};

// Factory: creates a GameMode from scenario config.
// Returns nullptr if no game mode is configured.
std::unique_ptr<GameMode> create_game_mode(const Scenario& scn);
