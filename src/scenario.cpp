#include "scenario.h"
#include "json.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

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

static ScenarioEntity::Role parse_entity_role(const std::string& role_str,
                                              EntityId id) {
    if (role_str == "drone") return ScenarioEntity::Role::Drone;
    if (role_str == "ground") return ScenarioEntity::Role::Ground;
    if (role_str == "target") return ScenarioEntity::Role::Target;
    throw std::runtime_error("unknown entity role '" + role_str +
                             "' for entity id " + std::to_string(id));
}

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
    int drone_count = 0;
    int ground_count = 0;
    int target_count = 0;

    for (const auto& ent : root["entities"].as_array()) {
        ScenarioEntity e;
        e.id = static_cast<EntityId>(ent["id"].as_number());
        e.role = parse_entity_role(ent["type"].as_string(), e.id);
        const auto& p = ent["pos"].as_array();
        e.position = {static_cast<float>(p[0].as_number()),
                      static_cast<float>(p[1].as_number())};
        const auto& v = ent["vel"].as_array();
        e.velocity = {static_cast<float>(v[0].as_number()),
                      static_cast<float>(v[1].as_number())};

        switch (e.role) {
            case ScenarioEntity::Role::Drone:  ++drone_count; break;
            case ScenarioEntity::Role::Ground: ++ground_count; break;
            case ScenarioEntity::Role::Target: ++target_count; break;
        }

        s.entities.push_back(e);
    }

    if (drone_count == 0)
        throw std::runtime_error("scenario validation failed: missing required role 'drone'");
    if (drone_count > 1)
        throw std::runtime_error("scenario validation failed: duplicate role 'drone' (expected exactly 1)");
    if (ground_count == 0)
        throw std::runtime_error("scenario validation failed: missing required role 'ground'");
    if (ground_count > 1)
        throw std::runtime_error("scenario validation failed: duplicate role 'ground' (expected exactly 1)");
    if (target_count == 0)
        throw std::runtime_error("scenario validation failed: missing required role 'target' (expected at least 1)");

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

    return s;
}
