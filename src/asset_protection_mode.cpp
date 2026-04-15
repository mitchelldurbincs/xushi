#include "asset_protection_mode.h"

#include <stdexcept>
#include <string>

void AssetProtectionMode::init(const Scenario& scn,
                               const std::vector<ScenarioEntity>& entities) {
    total_rounds_ = scn.rounds;
    assets_.clear();

    for (EntityId aid : scn.game_mode.asset_entity_ids) {
        bool found = false;
        for (const auto& e : entities) {
            if (e.id != aid) continue;
            if (e.team < 0)
                throw std::runtime_error(
                    "asset entity " + std::to_string(aid) + " has no team");
            AssetInfo info;
            info.entity_id = e.id;
            info.team = e.team;
            info.max_hp = e.max_hp;
            info.destroyed = e.hp <= 0;
            assets_.push_back(info);
            found = true;
            break;
        }
        if (!found)
            throw std::runtime_error(
                "asset entity " + std::to_string(aid) + " not found");
    }
    if (assets_.empty())
        throw std::runtime_error("asset_protection requires at least one asset");
}

void AssetProtectionMode::on_entity_damaged(int /*round*/, EntityId target,
                                            int /*hp_before*/, int hp_after) {
    for (auto& a : assets_)
        if (a.entity_id == target && hp_after <= 0)
            a.destroyed = true;
}

GameModeResult AssetProtectionMode::on_round_end(
    int round, const std::vector<ScenarioEntity>& entities) {
    for (const auto& a : assets_) {
        if (!a.destroyed) continue;
        int winner = -1;
        for (const auto& other : assets_)
            if (other.team != a.team && !other.destroyed) {
                winner = other.team;
                break;
            }
        GameModeResult r;
        r.finished = true;
        r.winning_team = winner;
        r.reason = "team " + std::to_string(a.team) + " asset destroyed";
        return r;
    }

    if (round >= total_rounds_ - 1) {
        float best = -1.0f;
        int best_team = -1;
        bool draw = false;
        for (const auto& a : assets_) {
            int hp = 0;
            for (const auto& e : entities)
                if (e.id == a.entity_id) { hp = e.hp; break; }
            float pct = a.max_hp > 0 ? static_cast<float>(hp) / a.max_hp : 0.0f;
            if (pct > best) { best = pct; best_team = a.team; draw = false; }
            else if (pct == best && a.team != best_team) draw = true;
        }
        GameModeResult r;
        r.finished = true;
        if (draw) { r.winning_team = -1; r.reason = "time expired, draw"; }
        else {
            r.winning_team = best_team;
            r.reason = "time expired, team " + std::to_string(best_team) + " healthier";
        }
        return r;
    }
    return GameModeResult{};
}
