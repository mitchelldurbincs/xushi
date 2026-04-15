#include "game_mode.h"

#include "asset_protection_mode.h"
#include "office_breach_mode.h"

#include <stdexcept>

std::unique_ptr<GameMode> create_game_mode(const Scenario& scn) {
    const auto& type = scn.game_mode.type;
    if (type.empty())
        return nullptr;
    if (type == "asset_protection")
        return std::make_unique<AssetProtectionMode>();
    if (type == "office_breach")
        return std::make_unique<OfficeBreachMode>();
    throw std::runtime_error("unknown game_mode type: " + type);
}
