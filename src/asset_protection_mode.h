#pragma once

#include "game_mode.h"
#include <vector>

// Asset Protection mode: each team has an asset entity to protect.
// Destroy the enemy's asset to win. If time runs out, the team with the
// healthier asset (by percentage of max vitality) wins.
struct AssetProtectionMode : GameMode {
    void init(const Scenario& scn,
              const std::vector<ScenarioEntity>& entities) override;
    void on_entity_damaged(int tick, EntityId target,
                           int vitality_before, int vitality_after) override;
    GameModeResult on_tick_end(int tick,
                               const std::vector<ScenarioEntity>& entities) override;

private:
    struct AssetInfo {
        EntityId entity_id = 0;
        int team = -1;
        int max_vitality = 100;
        bool destroyed = false;
    };
    std::vector<AssetInfo> assets_;
    int total_ticks_ = 0;
};
