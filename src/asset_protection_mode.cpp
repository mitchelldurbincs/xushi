#include "asset_protection_mode.h"
#include <stdexcept>
#include <string>

void AssetProtectionMode::init(const Scenario& scn,
                               const std::vector<ScenarioEntity>& entities) {
    total_ticks_ = scn.ticks;
    assets_.clear();

    for (int aid : scn.game_mode_config.asset_entity_ids) {
        bool found = false;
        for (const auto& e : entities) {
            if (e.id == static_cast<EntityId>(aid)) {
                if (e.team < 0)
                    throw std::runtime_error(
                        "asset entity " + std::to_string(aid) +
                        " has no team assignment");
                AssetInfo info;
                info.entity_id = e.id;
                info.team = e.team;
                info.max_vitality = e.max_vitality;
                info.destroyed = false;
                assets_.push_back(info);
                found = true;
                break;
            }
        }
        if (!found)
            throw std::runtime_error(
                "asset entity " + std::to_string(aid) + " not found in scenario");
    }

    if (assets_.empty())
        throw std::runtime_error("asset_protection mode requires at least one asset");
}

void AssetProtectionMode::on_entity_damaged(int /*tick*/, EntityId target,
                                            int /*vitality_before*/,
                                            int vitality_after) {
    for (auto& asset : assets_) {
        if (asset.entity_id == target && vitality_after <= 0)
            asset.destroyed = true;
    }
}

GameModeResult AssetProtectionMode::on_tick_end(
    int tick, const std::vector<ScenarioEntity>& entities) {

    // Check for destroyed assets
    for (const auto& asset : assets_) {
        if (!asset.destroyed) continue;

        // Find the surviving team(s)
        int winner = -1;
        for (const auto& other : assets_) {
            if (other.team != asset.team && !other.destroyed) {
                winner = other.team;
                break;
            }
        }

        GameModeResult result;
        result.finished = true;
        result.winning_team = winner;
        result.reason = "team " + std::to_string(asset.team) + " asset destroyed";
        return result;
    }

    // Check for time expiry (tick is 0-indexed, so last tick = total_ticks_ - 1)
    if (tick >= total_ticks_ - 1) {
        // Compare asset health percentages
        float best_pct = -1.0f;
        int best_team = -1;
        bool draw = false;

        for (const auto& asset : assets_) {
            // Find current vitality from entity list
            int current_vitality = 0;
            for (const auto& e : entities) {
                if (e.id == asset.entity_id) {
                    current_vitality = e.vitality;
                    break;
                }
            }
            float pct = (asset.max_vitality > 0)
                ? static_cast<float>(current_vitality) / static_cast<float>(asset.max_vitality)
                : 0.0f;

            if (pct > best_pct) {
                best_pct = pct;
                best_team = asset.team;
                draw = false;
            } else if (pct == best_pct && asset.team != best_team) {
                draw = true;
            }
        }

        GameModeResult result;
        result.finished = true;
        if (draw) {
            result.winning_team = -1;
            result.reason = "time expired, draw";
        } else {
            result.winning_team = best_team;
            result.reason = "time expired, team " + std::to_string(best_team) +
                            " has healthier asset";
        }
        return result;
    }

    return GameModeResult{};
}
