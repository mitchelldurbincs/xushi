#include "game_mode.h"
#include "asset_protection_mode.h"
#include <stdexcept>

std::unique_ptr<GameMode> create_game_mode(const Scenario& scn) {
    const auto& type = scn.game_mode_config.type;
    if (type.empty())
        return nullptr;
    if (type == "asset_protection")
        return std::make_unique<AssetProtectionMode>();
    throw std::runtime_error("unknown game_mode type: " + type);
}
