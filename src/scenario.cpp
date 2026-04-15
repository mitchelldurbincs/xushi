#include "scenario.h"

#include "json.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {

std::runtime_error field_error(const std::string& path,
                               const std::string& field,
                               const std::string& message) {
    return std::runtime_error(
        "invalid scenario '" + path + "': " + field + " " + message);
}

GridPos parse_cell(const JsonValue& v) {
    const auto& arr = v.as_array();
    if (arr.size() != 2)
        throw std::runtime_error("grid cell must be [x, y]");
    return GridPos{static_cast<int16_t>(arr[0].as_int()),
                   static_cast<int16_t>(arr[1].as_int())};
}

Facing parse_facing(const std::string& s) {
    if (s == "N" || s == "north") return Facing::North;
    if (s == "E" || s == "east")  return Facing::East;
    if (s == "S" || s == "south") return Facing::South;
    if (s == "W" || s == "west")  return Facing::West;
    throw std::runtime_error("unknown facing: " + s);
}

DoorState parse_door_state(const std::string& s) {
    if (s == "open")   return DoorState::OPEN;
    if (s == "closed") return DoorState::CLOSED;
    if (s == "locked") return DoorState::LOCKED;
    throw std::runtime_error("unknown door state: " + s);
}

DeviceKind parse_device_kind(const std::string& s) {
    if (s == "camera")   return DeviceKind::Camera;
    if (s == "relay")    return DeviceKind::Relay;
    if (s == "terminal") return DeviceKind::Terminal;
    if (s == "light")    return DeviceKind::Light;
    throw std::runtime_error("unknown device kind: " + s);
}

void parse_map(const std::string& path, const JsonValue& root, Scenario& s) {
    if (!root.has("map"))
        throw field_error(path, "map", "is required");
    const auto& m = root["map"];
    if (!m.has("rows"))
        throw field_error(path, "map.rows", "is required (array of strings)");
    const auto& rows = m["rows"].as_array();
    s.ascii_map.clear();
    for (const auto& r : rows)
        s.ascii_map.push_back(r.as_string());
    s.grid = GridMap::from_ascii(s.ascii_map);

    if (m.has("doors")) {
        for (const auto& d : m["doors"].as_array()) {
            GridPos a = parse_cell(d["a"]);
            GridPos b = parse_cell(d["b"]);
            DoorState st = DoorState::CLOSED;
            if (d.has("state"))
                st = parse_door_state(d["state"].as_string());
            s.grid.add_door(a, b, st);
        }
    }
    s.grid.recompute_rooms();
}

void parse_entities(const std::string& path, const JsonValue& root, Scenario& s) {
    if (!root.has("entities"))
        throw field_error(path, "entities", "is required");
    std::unordered_set<EntityId> seen;
    for (const auto& e : root["entities"].as_array()) {
        ScenarioEntity ent;
        ent.id = static_cast<EntityId>(e["id"].as_int());
        if (!seen.insert(ent.id).second)
            throw field_error(path, "entities.id",
                              "duplicate id " + std::to_string(ent.id));
        ent.role_name = e.has("name") ? e["name"].as_string() : "";
        std::string kind = e.has("kind") ? e["kind"].as_string() : "operator";
        if (kind == "operator")      ent.kind = EntityKind::Operator;
        else if (kind == "drone")    ent.kind = EntityKind::Drone;
        else throw field_error(path, "entities.kind", "unknown '" + kind + "'");
        ent.pos = parse_cell(e["pos"]);
        ent.team = e["team"].as_int();

        if (e.has("hp"))           ent.hp = e["hp"].as_int();
        if (e.has("max_hp"))       ent.max_hp = e["max_hp"].as_int();
        if (e.has("max_ap"))       ent.max_ap = e["max_ap"].as_int();
        if (e.has("ammo"))         ent.ammo = e["ammo"].as_int();
        if (e.has("vision_range")) ent.vision_range = e["vision_range"].as_int();
        if (e.has("weapon_range")) ent.weapon_range = e["weapon_range"].as_int();
        if (e.has("weapon_base_hit"))
            ent.weapon_base_hit = static_cast<float>(e["weapon_base_hit"].as_number());
        if (e.has("weapon_damage")) ent.weapon_damage = e["weapon_damage"].as_int();

        if (ent.kind == EntityKind::Drone) {
            if (e.has("battery"))        ent.drone_battery = e["battery"].as_int();
            if (e.has("vision_range"))   ent.drone_vision_range = e["vision_range"].as_int();
            if (e.has("move_range"))     ent.drone_move_range = e["move_range"].as_int();
            if (e.has("deployed"))       ent.drone_deployed = e["deployed"].as_bool();
        }
        s.entities.push_back(ent);
    }
}

void parse_devices(const JsonValue& root, Scenario& s) {
    if (!root.has("devices")) return;
    uint32_t auto_id = 1;
    for (const auto& d : root["devices"].as_array()) {
        Device dev;
        dev.id = d.has("id") ? static_cast<uint32_t>(d["id"].as_int()) : auto_id++;
        dev.kind = parse_device_kind(d["kind"].as_string());
        dev.pos = parse_cell(d["pos"]);
        if (d.has("team")) dev.team = d["team"].as_int();
        if (d.has("facing")) dev.facing = parse_facing(d["facing"].as_string());
        if (d.has("range")) dev.range = d["range"].as_int();
        if (d.has("lights_on")) dev.lights_on = d["lights_on"].as_bool();
        s.devices.push_back(dev);
    }
}

void parse_belief_config(const JsonValue& root, Scenario& s) {
    if (!root.has("belief")) return;
    const auto& b = root["belief"];
    if (b.has("fresh_rounds"))
        s.belief.fresh_rounds = b["fresh_rounds"].as_int();
    if (b.has("stale_rounds"))
        s.belief.stale_rounds = b["stale_rounds"].as_int();
    if (b.has("uncertainty_growth_per_round"))
        s.belief.uncertainty_growth_per_round =
            static_cast<float>(b["uncertainty_growth_per_round"].as_number());
    if (b.has("confidence_decay_per_round"))
        s.belief.confidence_decay_per_round =
            static_cast<float>(b["confidence_decay_per_round"].as_number());
}

void parse_game_mode(const JsonValue& root, Scenario& s) {
    if (!root.has("game_mode")) return;
    const auto& gm = root["game_mode"];
    s.game_mode.type = gm["type"].as_string();
    if (gm.has("objective_cell"))
        s.game_mode.objective_cell = parse_cell(gm["objective_cell"]);
    if (gm.has("assets"))
        for (const auto& a : gm["assets"].as_array())
            s.game_mode.asset_entity_ids.push_back(static_cast<EntityId>(a.as_int()));
}

void validate(const std::string& path, const Scenario& s) {
    if (s.rounds <= 0)
        throw field_error(path, "rounds", "must be > 0");
    if (s.grid.width() <= 0 || s.grid.height() <= 0)
        throw field_error(path, "map", "empty");
    if (s.entities.empty())
        throw field_error(path, "entities", "must contain at least one entity");

    for (const auto& e : s.entities) {
        if (!s.grid.in_bounds(e.pos))
            throw field_error(path, "entities.pos",
                              "out of bounds for entity " + std::to_string(e.id));
        if (e.team < 0)
            throw field_error(path, "entities.team",
                              "must be >= 0 for entity " + std::to_string(e.id));
    }
    for (const auto& d : s.devices) {
        if (!s.grid.in_bounds(d.pos))
            throw field_error(path, "devices.pos",
                              "out of bounds for device " + std::to_string(d.id));
    }
    if (s.game_mode.type == "office_breach" &&
        !s.grid.in_bounds(s.game_mode.objective_cell))
        throw field_error(path, "game_mode.objective_cell", "out of bounds");
}

}  // namespace

Scenario load_scenario(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("cannot open scenario file: " + path);
    std::stringstream buf;
    buf << file.rdbuf();
    JsonValue root = json_parse(buf.str());

    Scenario s;
    s.seed = static_cast<uint64_t>(root["seed"].as_number());
    if (root.has("rounds")) s.rounds = root["rounds"].as_int();

    parse_map(path, root, s);
    parse_entities(path, root, s);
    parse_devices(root, s);
    parse_belief_config(root, s);
    parse_game_mode(root, s);
    validate(path, s);
    return s;
}
