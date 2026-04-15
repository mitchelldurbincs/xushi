#pragma once

#include "scenario.h"

#include <memory>
#include <string>
#include <vector>

struct GameModeResult {
    bool finished = false;
    int winning_team = -1;   // -1 = draw / timeout
    std::string reason;
};

// Abstract interface for game modes. Called by SimEngine at well-defined
// points in the round loop.
struct GameMode {
    virtual ~GameMode() = default;

    virtual void init(const Scenario& scn, const std::vector<ScenarioEntity>& entities) = 0;

    virtual void on_round_start(int /*round*/, const std::vector<ScenarioEntity>& /*entities*/) {}

    virtual void on_entity_damaged(int /*round*/, EntityId /*target*/,
                                   int /*hp_before*/, int /*hp_after*/) {}

    // Called at end of round. Return finished=true to end the simulation.
    virtual GameModeResult on_round_end(int round,
                                         const std::vector<ScenarioEntity>& entities) = 0;
};

// Factory: creates a GameMode from scenario config.
// Returns nullptr if no game mode is configured.
std::unique_ptr<GameMode> create_game_mode(const Scenario& scn);
