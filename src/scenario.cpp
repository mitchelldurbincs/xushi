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
    for (const auto& ent : root["entities"].as_array()) {
        ScenarioEntity e;
        e.id = static_cast<EntityId>(ent["id"].as_number());
        e.type = ent["type"].as_string();
        const auto& p = ent["pos"].as_array();
        e.position = {static_cast<float>(p[0].as_number()),
                      static_cast<float>(p[1].as_number())};
        const auto& v = ent["vel"].as_array();
        e.velocity = {static_cast<float>(v[0].as_number()),
                      static_cast<float>(v[1].as_number())};
        s.entities.push_back(e);
    }

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
        s.belief.uncertainty_growth_rate = static_cast<float>(b.number_or("uncertainty_growth", 0.5));
        s.belief.confidence_decay_rate = static_cast<float>(b.number_or("confidence_decay", 0.05));
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
    validate_non_negative(path, "belief.uncertainty_growth_rate", s.belief.uncertainty_growth_rate);
    validate_non_negative(path, "belief.confidence_decay_rate", s.belief.confidence_decay_rate);

    return s;
}
