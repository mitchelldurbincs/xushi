#include "scenario.h"
#include "json.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {

std::runtime_error make_field_error(const std::string& path,
                                    const std::string& field,
                                    const std::string& message) {
    return std::runtime_error(
        "invalid scenario '" + path + "': " + field + " " + message);
}

template <typename T>
std::string to_string_value(T value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <typename T>
void validate_non_negative(const std::string& path,
                           const char* field_name,
                           T value) {
    if (value < static_cast<T>(0)) {
        throw make_field_error(path, field_name,
                               "must be >= 0, got " + to_string_value(value));
    }
}

template <typename T>
void validate_positive(const std::string& path,
                       const char* field_name,
                       T value) {
    if (value <= static_cast<T>(0)) {
        throw make_field_error(path, field_name,
                               "must be > 0, got " + to_string_value(value));
    }
}

void parse_top_level_config(const JsonValue& root, Scenario& s) {
    s.seed = static_cast<uint64_t>(root["seed"].as_number());
    s.dt = static_cast<float>(root.number_or("dt", 1.0));
    s.ticks = root.int_or("ticks", 100);
    s.max_sensor_range = static_cast<float>(root.number_or("max_sensor_range", 80.0));

    for (const auto& obs : root["obstacles"].as_array()) {
        const auto& mn = obs["min"].as_array();
        const auto& mx = obs["max"].as_array();
        Rect r;
        r.min = {static_cast<float>(mn[0].as_number()),
                 static_cast<float>(mn[1].as_number())};
        r.max = {static_cast<float>(mx[0].as_number()),
                 static_cast<float>(mx[1].as_number())};
        s.obstacles.push_back(r);
    }

    if (root.has("channel")) {
        const auto& ch = root["channel"];
        s.channel.base_latency_ticks = ch.int_or("base_latency", 3);
        s.channel.latency_per_distance = static_cast<float>(ch.number_or("per_distance", 0.0));
        s.channel.loss_probability = static_cast<float>(ch.number_or("loss", 0.1));
    }

    if (root.has("belief")) {
        const auto& b = root["belief"];
        s.belief.fresh_ticks = b.int_or("fresh_ticks", 5);
        s.belief.stale_ticks = b.int_or("stale_ticks", 10);
        s.belief.uncertainty_growth_per_second = static_cast<float>(
            b.number_or("uncertainty_growth_per_second",
                        b.number_or("uncertainty_growth", 0.5)));
        s.belief.confidence_decay_per_second = static_cast<float>(
            b.number_or("confidence_decay_per_second",
                        b.number_or("confidence_decay", 0.05)));
        s.belief.negative_evidence_factor = static_cast<float>(
            b.number_or("negative_evidence_factor", 0.3));
    }

    if (root.has("perception")) {
        const auto& p = root["perception"];
        s.perception.miss_rate = static_cast<float>(p.number_or("miss_rate", 0.0));
        s.perception.false_positive_rate = static_cast<float>(p.number_or("false_positive_rate", 0.0));
        s.perception.class_confusion_rate = static_cast<float>(p.number_or("class_confusion_rate", 0.0));
    }

    if (root.has("game_mode")) {
        const auto& gm = root["game_mode"];
        s.game_mode_config.type = gm["type"].as_string();
        if (gm.has("assets")) {
            for (const auto& a : gm["assets"].as_array())
                s.game_mode_config.asset_entity_ids.push_back(a.as_int());
        }
    }
}

void parse_effect_profiles(const JsonValue& root, Scenario& s) {
    if (!root.has("effect_profiles"))
        return;

    const auto& profiles = root["effect_profiles"].as_array();
    for (const auto& profile : profiles) {
        Scenario::EffectProfile p;
        p.name = profile["name"].as_string();
        p.range = static_cast<float>(profile["range"].as_number());
        if (profile.has("requires_los"))
            p.requires_los = profile["requires_los"].as_bool();
        p.identity_threshold = static_cast<float>(
            profile.number_or("identity_threshold", 0.0));
        p.corroboration_threshold = static_cast<float>(
            profile.number_or("corroboration_threshold", 0.0));
        p.cooldown_ticks = profile.int_or("cooldown_ticks", 0);
        p.ammo_cost = profile.int_or("ammo_cost", 0);

        p.hit_probability = static_cast<float>(profile.number_or("hit_probability", 1.0));
        p.vitality_delta_min = profile.int_or("vitality_delta_min", 0);
        p.vitality_delta_max = profile.int_or("vitality_delta_max", p.vitality_delta_min);

        if (profile.has("roe_flags")) {
            for (const auto& flag : profile["roe_flags"].as_array())
                p.roe_flags.push_back(flag.as_string());
        }
        s.effect_profiles.push_back(p);
    }
}

void parse_entities(const std::string& path, const JsonValue& root, Scenario& s) {
    std::unordered_set<EntityId> seen_ids;

    const auto& entities = root["entities"].as_array();
    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& ent = entities[i];
        ScenarioEntity e;
        e.id = static_cast<EntityId>(ent["id"].as_number());
        if (!seen_ids.insert(e.id).second) {
            throw make_field_error(path,
                                   "entities[" + std::to_string(i) + "].id",
                                   "duplicate entity id " + std::to_string(e.id));
        }

        e.role_name = ent["type"].as_string();
        const auto& p = ent["pos"].as_array();
        e.position = {static_cast<float>(p[0].as_number()),
                      static_cast<float>(p[1].as_number())};
        const auto& v = ent["vel"].as_array();
        e.velocity = {static_cast<float>(v[0].as_number()),
                      static_cast<float>(v[1].as_number())};

        if (ent.has("can_sense"))     e.can_sense = ent["can_sense"].as_bool();
        if (ent.has("can_track"))     e.can_track = ent["can_track"].as_bool();
        if (ent.has("is_observable")) e.is_observable = ent["is_observable"].as_bool();
        if (ent.has("can_engage"))    e.can_engage = ent["can_engage"].as_bool();

        if (ent.has("team"))      e.team = ent["team"].as_int();
        if (ent.has("class_id"))  e.class_id = ent["class_id"].as_int();

        if (ent.has("max_vitality")) e.max_vitality = ent["max_vitality"].as_int();
        if (ent.has("vitality"))     e.vitality = ent["vitality"].as_int();
        if (e.vitality > e.max_vitality)
            e.vitality = e.max_vitality;

        if (ent.has("ammo")) e.ammo = ent["ammo"].as_int();
        if (ent.has("cooldown_ticks_remaining"))
            e.cooldown_ticks_remaining = ent["cooldown_ticks_remaining"].as_int();
        if (ent.has("allowed_effect_profile_indices")) {
            for (const auto& idx : ent["allowed_effect_profile_indices"].as_array())
                e.allowed_effect_profile_indices.push_back(idx.as_int());
        }

        if (ent.has("waypoints")) {
            const auto& wps = ent["waypoints"].as_array();
            if (wps.empty()) {
                throw std::runtime_error("entity " + std::to_string(e.id) +
                                         " has waypoints key but empty list");
            }
            for (const auto& wp : wps) {
                const auto& coords = wp.as_array();
                e.waypoints.push_back({
                    static_cast<float>(coords[0].as_number()),
                    static_cast<float>(coords[1].as_number())
                });
            }
            e.speed = static_cast<float>(ent["speed"].as_number());
            validate_positive(path, "entity speed", e.speed);

            if (ent.has("waypoint_mode")) {
                std::string mode_str = ent["waypoint_mode"].as_string();
                if (mode_str == "stop")
                    e.waypoint_mode = ScenarioEntity::WaypointMode::Stop;
                else if (mode_str == "loop")
                    e.waypoint_mode = ScenarioEntity::WaypointMode::Loop;
                else
                    throw std::runtime_error("unknown waypoint_mode '" + mode_str +
                                             "' for entity id " + std::to_string(e.id));
            }

            if (ent.has("branch_points")) {
                const auto& bp = ent["branch_points"].as_object();
                for (const auto& [key, val] : bp) {
                    int wp_idx = std::stoi(key);
                    if (wp_idx < 0 || wp_idx >= static_cast<int>(e.waypoints.size()))
                        throw std::runtime_error("branch_points index " + key +
                                                 " out of range for entity " + std::to_string(e.id));
                    std::vector<int> successors;
                    for (const auto& s_val : val.as_array()) {
                        int succ = s_val.as_int();
                        if (succ < 0 || succ >= static_cast<int>(e.waypoints.size()))
                            throw std::runtime_error("branch successor " + std::to_string(succ) +
                                                     " out of range for entity " + std::to_string(e.id));
                        successors.push_back(succ);
                    }
                    e.branch_points[wp_idx] = successors;
                }
            }
        }

        if (e.waypoints.empty() && ent.has("speed")) {
            e.speed = static_cast<float>(ent["speed"].as_number());
            validate_positive(path, "entity speed", e.speed);
        }

        s.entities.push_back(e);
    }
}

void parse_policy_config(const JsonValue& root, Scenario& s) {
    if (!root.has("policy"))
        return;

    const auto& pol = root["policy"];
    s.policy_config.type = pol["type"].as_string();
    if (s.policy_config.type == "patrol") {
        if (!pol.has("routes"))
            throw std::runtime_error("patrol policy requires 'routes' object");
        const auto& routes = pol["routes"].as_object();
        for (const auto& [key, val] : routes) {
            EntityId eid = static_cast<EntityId>(std::stoi(key));
            std::vector<Vec2> waypoints;
            for (const auto& wp : val.as_array()) {
                const auto& coords = wp.as_array();
                waypoints.push_back({
                    static_cast<float>(coords[0].as_number()),
                    static_cast<float>(coords[1].as_number())
                });
            }
            if (waypoints.empty())
                throw std::runtime_error("patrol route for entity " + key + " is empty");
            s.policy_config.patrol_routes[eid] = waypoints;
        }
    } else if (s.policy_config.type != "null" && !s.policy_config.type.empty()) {
        throw std::runtime_error("unknown policy type '" + s.policy_config.type + "'");
    }
}

void validate_scenario(const std::string& path, const Scenario& s) {
    validate_non_negative(path, "ticks", s.ticks);
    validate_positive(path, "dt", s.dt);
    validate_positive(path, "max_sensor_range", s.max_sensor_range);
    validate_non_negative(path, "channel.base_latency_ticks", s.channel.base_latency_ticks);
    validate_non_negative(path, "channel.latency_per_distance", s.channel.latency_per_distance);
    if (s.channel.loss_probability < 0.0f || s.channel.loss_probability > 1.0f) {
        throw make_field_error(path, "channel.loss_probability",
                               "must be in [0, 1], got " + to_string_value(s.channel.loss_probability));
    }
    validate_non_negative(path, "belief.fresh_ticks", s.belief.fresh_ticks);
    validate_non_negative(path, "belief.stale_ticks", s.belief.stale_ticks);
    validate_non_negative(path, "belief.uncertainty_growth_per_second",
                          s.belief.uncertainty_growth_per_second);
    validate_non_negative(path, "belief.confidence_decay_per_second",
                          s.belief.confidence_decay_per_second);
    if (s.belief.negative_evidence_factor < 0.0f || s.belief.negative_evidence_factor > 1.0f) {
        throw make_field_error(path, "belief.negative_evidence_factor",
                               "must be in [0, 1], got " + to_string_value(s.belief.negative_evidence_factor));
    }

    validate_non_negative(path, "perception.miss_rate", s.perception.miss_rate);
    validate_non_negative(path, "perception.false_positive_rate", s.perception.false_positive_rate);
    validate_non_negative(path, "perception.class_confusion_rate", s.perception.class_confusion_rate);

    for (size_t i = 0; i < s.effect_profiles.size(); ++i) {
        const auto& p = s.effect_profiles[i];
        const std::string prefix = "effect_profiles[" + std::to_string(i) + "]";
        if (p.hit_probability < 0.0f || p.hit_probability > 1.0f) {
            throw make_field_error(path, prefix + ".hit_probability",
                                   "must be in [0, 1], got " + to_string_value(p.hit_probability));
        }
        if (p.vitality_delta_min > p.vitality_delta_max) {
            throw make_field_error(path, prefix + ".vitality_delta",
                                   "min > max is invalid");
        }
        if (p.cooldown_ticks < 0) {
            throw make_field_error(path, prefix + ".cooldown_ticks",
                                   "must be >= 0, got " + std::to_string(p.cooldown_ticks));
        }
        if (p.ammo_cost < 0) {
            throw make_field_error(path, prefix + ".ammo_cost",
                                   "must be >= 0, got " + std::to_string(p.ammo_cost));
        }
    }

    int sensor_count = 0;
    int tracker_count = 0;
    int observable_count = 0;
    for (size_t i = 0; i < s.entities.size(); ++i) {
        const auto& e = s.entities[i];
        if (e.can_sense) ++sensor_count;
        if (e.can_track) ++tracker_count;
        if (e.is_observable) ++observable_count;

        const std::string eprefix =
            "entities[" + std::to_string(i) + "] (id=" + std::to_string(e.id) + ")";
        if (e.ammo < 0) {
            throw make_field_error(path, eprefix + ".ammo",
                                   "must be >= 0, got " + std::to_string(e.ammo));
        }
        if (e.cooldown_ticks_remaining < 0) {
            throw make_field_error(path, eprefix + ".cooldown_ticks_remaining",
                                   "must be >= 0, got " +
                                       std::to_string(e.cooldown_ticks_remaining));
        }
        for (size_t j = 0; j < e.allowed_effect_profile_indices.size(); ++j) {
            const int idx = e.allowed_effect_profile_indices[j];
            if (idx < 0 || idx >= static_cast<int>(s.effect_profiles.size())) {
                throw make_field_error(
                    path,
                    eprefix + ".allowed_effect_profile_indices[" + std::to_string(j) + "]",
                    "references effect_profiles[" + std::to_string(idx) +
                        "] but valid range is [0, " +
                        std::to_string(static_cast<int>(s.effect_profiles.size()) - 1) + "]");
            }
        }
    }

    if (sensor_count == 0)
        throw std::runtime_error("scenario validation failed: no entity with sensing capability");
    if (tracker_count == 0)
        throw std::runtime_error("scenario validation failed: no entity with tracking capability");
    if (observable_count == 0)
        throw std::runtime_error("scenario validation failed: no observable entity");
}

} // namespace

Scenario load_scenario(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("cannot open scenario file: " + path);

    std::stringstream buf;
    buf << file.rdbuf();
    JsonValue root = json_parse(buf.str());

    Scenario s;
    parse_top_level_config(root, s);
    parse_effect_profiles(root, s);
    parse_entities(path, root, s);
    parse_policy_config(root, s);
    validate_scenario(path, s);
    return s;
}
