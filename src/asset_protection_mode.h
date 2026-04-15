#pragma once

#include "game_mode.h"

#include <vector>

// Asset Protection mode: each team has one or more asset entities to protect.
// Destroy the enemy asset to win. If the round limit is reached, the team
// whose asset has the higher HP percentage wins.
struct AssetProtectionMode : GameMode {
    void init(const Scenario& scn,
              const std::vector<ScenarioEntity>& entities) override;
    void on_entity_damaged(int round, EntityId target,
                           int hp_before, int hp_after) override;
    GameModeResult on_round_end(int round,
                                const std::vector<ScenarioEntity>& entities) override;

private:
    struct AssetInfo {
        EntityId entity_id = 0;
        int team = -1;
        int max_hp = 100;
        bool destroyed = false;
    };
    std::vector<AssetInfo> assets_;
    int total_rounds_ = 0;
};
