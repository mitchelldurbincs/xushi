#pragma once

#include "game_mode.h"

// Office Breach mode (contract §10, first scenario).
// - Attacker (team 0) wins if any attacker operator interacts with the
//   objective terminal, OR all defenders are eliminated.
// - Defender (team 1) wins if all attackers are eliminated OR the round
//   limit is reached without the attacker achieving their objective.
// Note: interact events are tracked by SimEngine and reported via
// on_entity_damaged is not applicable; we use a flag set from outside
// once interact actions land. For this migration step, we only implement
// the elimination and timeout branches since interact actions aren't
// wired yet.
struct OfficeBreachMode : GameMode {
    void init(const Scenario& scn,
              const std::vector<ScenarioEntity>& entities) override;
    GameModeResult on_round_end(int round,
                                const std::vector<ScenarioEntity>& entities) override;

    // Called by SimEngine when an attacker operator uses INTERACT on the
    // objective cell. Results in an immediate attacker win at end of round.
    void notify_objective_interacted() { objective_done_ = true; }

private:
    int total_rounds_ = 12;
    int attacker_team_ = 0;
    int defender_team_ = 1;
    bool objective_done_ = false;
};
