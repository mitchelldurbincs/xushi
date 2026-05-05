#pragma once

#include "belief.h"
#include "grid.h"
#include "types.h"

#include <cstdint>
#include <string>
#include <vector>

// Contract-defined entity kinds (v1).
// Devices (camera, light, terminal, relay) are map objects, not entities,
// but we carry their state here to keep scenario loading in one place.
enum class EntityKind : uint8_t {
    Operator = 0,
    Drone    = 1,
};

struct ScenarioEntity {
    EntityId id = 0;
    std::string role_name;         // display label
    EntityKind kind = EntityKind::Operator;
    GridPos pos{};                 // current cell
    int team = -1;                 // faction id; operators always have a team

    // Common stats.
    int hp = 100;
    int max_hp = 100;

    // Operator-only (contract §4).
    int max_ap = 3;
    int ammo = 10;
    int vision_range = 10;
    int weapon_range = 7;          // rifle default
    float weapon_base_hit = 0.70f;
    int weapon_damage = 50;

    // Drone-only (contract §5).
    int drone_battery = 8;
    int drone_vision_range = 6;
    int drone_move_range = 3;
    bool drone_deployed = false;   // operators carry their team drone until deployed

    // Operator-only (contract §7). Persists across activations within a round
    // until consumed by a trigger or the operator's next activation.
    bool overwatch_active = false;
};

// Device objects on the map (contract §3).
enum class DeviceKind : uint8_t {
    Camera   = 0,
    Relay    = 1,
    Terminal = 2,
    Light    = 3,
};

enum class Facing : uint8_t { North = 0, East = 1, South = 2, West = 3 };

struct Device {
    uint32_t id = 0;
    DeviceKind kind = DeviceKind::Camera;
    GridPos pos{};
    int team = -1;                 // owner (-1 = neutral)
    Facing facing = Facing::North; // cameras only
    int range = 0;                 // cameras/relays
    bool lights_on = true;         // light switches only
};

struct Scenario {
    // Required.
    uint64_t seed = 0;
    int rounds = 12;               // contract §2: default R = 12

    // Grid is mandatory; the ASCII source is kept for replay/logging.
    std::vector<std::string> ascii_map;
    GridMap grid;

    // Scenario content.
    std::vector<ScenarioEntity> entities;
    std::vector<Device> devices;

    // Tuning.
    BeliefConfig belief{};

    // Optional win-condition configuration.
    struct GameModeConfig {
        std::string type;                 // "", "office_breach", "asset_protection"
        GridPos objective_cell{};         // office_breach terminal
        std::vector<EntityId> asset_entity_ids;  // asset_protection assets
    };
    GameModeConfig game_mode{};
};

// Load a scenario from a JSON file. Throws std::runtime_error on failure.
Scenario load_scenario(const std::string& path);
