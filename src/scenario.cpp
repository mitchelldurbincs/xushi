#include "scenario.h"
#include "json.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {

template <typename T>
void validate_non_negative(const std::string& path,
                           const char* field_name,
                           T value) {
    if (value < static_cast<T>(0)) {
        std::ostringstream oss;
        oss << "invalid scenario '" << path << "': " << field_name
            << " must be >= 0, got " << value;
        throw std::runtime_error(oss.str());
    }
}

template <typename T>
void validate_positive(const std::string& path,
                       const char* field_name,
                       T value) {
    if (value <= static_cast<T>(0)) {
        std::ostringstream oss;
        oss << "invalid scenario '" << path << "': " << field_name
            << " must be > 0, got " << value;
        throw std::runtime_error(oss.str());
    }
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

    s.seed = static_cast<uint64_t>(root["seed"].as_number());
    s.dt = static_cast<float>(root.number_or("dt", 1.0));
    s.ticks = root.int_or("ticks", 100);
    s.max_sensor_range = static_cast<float>(root.number_or("max_sensor_range", 80.0));

    // Obstacles
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

    // Entities
    int sensor_count = 0;
    int tracker_count = 0;
    int observable_count = 0;
    std::unordered_set<EntityId> seen_ids;

    for (const auto& ent : root["entities"].as_array()) {
        ScenarioEntity e;
        e.id = static_cast<EntityId>(ent["id"].as_number());
        if (!seen_ids.insert(e.id).second) {
            throw std::runtime_error(
                "invalid scenario '" + path + "': duplicate entity id " + std::to_string(e.id));
        }
        e.role_name = ent["type"].as_string();
        const auto& p = ent["pos"].as_array();
        e.position = {static_cast<float>(p[0].as_number()),
                      static_cast<float>(p[1].as_number())};
        const auto& v = ent["vel"].as_array();
        e.velocity = {static_cast<float>(v[0].as_number()),
                      static_cast<float>(v[1].as_number())};

        // Capability overrides (optional)
        if (ent.has("can_sense"))     e.can_sense     = ent["can_sense"].as_bool();
        if (ent.has("can_track"))     e.can_track     = ent["can_track"].as_bool();
        if (ent.has("is_observable")) e.is_observable = ent["is_observable"].as_bool();

        if (ent.has("class_id"))      e.class_id = ent["class_id"].as_int();

        // Waypoints (optional)
        if (ent.has("waypoints")) {
            const auto& wps = ent["waypoints"].as_array();
            if (wps.empty())
                throw std::runtime_error("entity " + std::to_string(e.id) +
                                         " has waypoints key but empty list");
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

            // Branch points (optional)
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

        // Speed without waypoints (for task-directed entities)
        if (e.waypoints.empty() && ent.has("speed")) {
            e.speed = static_cast<float>(ent["speed"].as_number());
            validate_positive(path, "entity speed", e.speed);
        }

        if (e.can_sense)     ++sensor_count;
        if (e.can_track)     ++tracker_count;
        if (e.is_observable) ++observable_count;

        s.entities.push_back(e);
    }

    if (sensor_count == 0)
        throw std::runtime_error("scenario validation failed: no entity with sensing capability");
    if (tracker_count == 0)
        throw std::runtime_error("scenario validation failed: no entity with tracking capability");
    if (observable_count == 0)
        throw std::runtime_error("scenario validation failed: no observable entity");

    // Channel (optional)
    if (root.has("channel")) {
        const auto& ch = root["channel"];
        s.channel.base_latency_ticks = ch.int_or("base_latency", 3);
        s.channel.latency_per_distance = static_cast<float>(ch.number_or("per_distance", 0.0));
        s.channel.loss_probability = static_cast<float>(ch.number_or("loss", 0.1));
    }

    // Belief config (optional)
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

    // Perception config (optional)
    if (root.has("perception")) {
        const auto& p = root["perception"];
        s.perception.miss_rate = static_cast<float>(p.number_or("miss_rate", 0.0));
        s.perception.false_positive_rate = static_cast<float>(p.number_or("false_positive_rate", 0.0));
        s.perception.class_confusion_rate = static_cast<float>(p.number_or("class_confusion_rate", 0.0));
    }

    validate_non_negative(path, "ticks", s.ticks);
    validate_positive(path, "dt", s.dt);
    validate_positive(path, "max_sensor_range", s.max_sensor_range);
    validate_non_negative(path, "channel.base_latency_ticks", s.channel.base_latency_ticks);
    validate_non_negative(path, "channel.latency_per_distance", s.channel.latency_per_distance);
    if (s.channel.loss_probability < 0.0f || s.channel.loss_probability > 1.0f) {
        std::ostringstream oss;
        oss << "invalid scenario '" << path << "': channel.loss_probability"
            << " must be in [0, 1], got " << s.channel.loss_probability;
        throw std::runtime_error(oss.str());
    }
    validate_non_negative(path, "belief.fresh_ticks", s.belief.fresh_ticks);
    validate_non_negative(path, "belief.stale_ticks", s.belief.stale_ticks);
    validate_non_negative(path, "belief.uncertainty_growth_per_second", s.belief.uncertainty_growth_per_second);
    validate_non_negative(path, "belief.confidence_decay_per_second", s.belief.confidence_decay_per_second);
    if (s.belief.negative_evidence_factor < 0.0f || s.belief.negative_evidence_factor > 1.0f) {
        std::ostringstream oss;
        oss << "invalid scenario '" << path << "': belief.negative_evidence_factor"
            << " must be in [0, 1], got " << s.belief.negative_evidence_factor;
        throw std::runtime_error(oss.str());
    }

    validate_non_negative(path, "perception.miss_rate", s.perception.miss_rate);
    validate_non_negative(path, "perception.false_positive_rate", s.perception.false_positive_rate);
    validate_non_negative(path, "perception.class_confusion_rate", s.perception.class_confusion_rate);

    // Policy config (optional)
    if (root.has("policy")) {
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

    return s;
}
