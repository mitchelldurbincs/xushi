#include "viewer.h"
#include "../src/replay.h"
#include "../src/path_resolve.h"
#include "raylib.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void include_point(WorldBounds& bounds, float x, float y) {
    if (!bounds.has_points) {
        bounds.min_x = x;
        bounds.max_x = x;
        bounds.min_y = y;
        bounds.max_y = y;
        bounds.has_points = true;
        return;
    }

    bounds.min_x = std::min(bounds.min_x, x);
    bounds.min_y = std::min(bounds.min_y, y);
    bounds.max_x = std::max(bounds.max_x, x);
    bounds.max_y = std::max(bounds.max_y, y);
}
} // namespace

// --- JSON accessor helpers (used only during replay loading) ---

static const char* json_type_name(JsonValue::Type type) {
    switch (type) {
        case JsonValue::NUL: return "null";
        case JsonValue::BOOL: return "bool";
        case JsonValue::NUMBER: return "number";
        case JsonValue::STRING: return "string";
        case JsonValue::ARRAY: return "array";
        case JsonValue::OBJECT: return "object";
    }
    return "unknown";
}

static const JsonValue& get_required_value_of_type(const JsonValue& obj, const std::string& key,
                                                   JsonValue::Type expected_type,
                                                   const char* expected_desc,
                                                   const std::string& context) {
    if (!obj.has(key))
        throw std::runtime_error(context + ": missing required key '" + key + "'");
    const auto& v = obj[key];
    if (v.type != expected_type) {
        throw std::runtime_error(context + ": key '" + key + "' expected " + expected_desc + ", got " +
                                 json_type_name(v.type));
    }
    return v;
}

static const std::string& get_required_string(const JsonValue& obj, const std::string& key, const std::string& context) {
    return get_required_value_of_type(obj, key, JsonValue::STRING, "string", context).as_string();
}

static int get_required_int(const JsonValue& obj, const std::string& key, const std::string& context) {
    return get_required_value_of_type(obj, key, JsonValue::NUMBER, "number/int", context).as_int();
}

static double get_required_number(const JsonValue& obj, const std::string& key, const std::string& context) {
    return get_required_value_of_type(obj, key, JsonValue::NUMBER, "number", context).as_number();
}

static const std::vector<JsonValue>& get_required_array(const JsonValue& obj, const std::string& key, size_t min_len,
                                                        const std::string& context) {
    const auto& arr = get_required_value_of_type(obj, key, JsonValue::ARRAY, "array", context).as_array();
    if (arr.size() < min_len)
        throw std::runtime_error(context + ": key '" + key + "' expected array len >= " + std::to_string(min_len));
    return arr;
}

// --- World bounds ---

WorldBounds compute_world_bounds(const Scenario& scenario) {
    WorldBounds bounds = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        false
    };

    for (const auto& obs : scenario.obstacles) {
        include_point(bounds, obs.min.x, obs.min.y);
        include_point(bounds, obs.max.x, obs.max.y);
    }

    const float replay_seconds = scenario.dt * static_cast<float>(scenario.ticks);
    for (const auto& ent : scenario.entities) {
        include_point(bounds, ent.position.x, ent.position.y);

        if (!ent.waypoints.empty()) {
            for (const auto& wp : ent.waypoints)
                include_point(bounds, wp.x, wp.y);
        } else {
            const float end_x = ent.position.x + ent.velocity.x * replay_seconds;
            const float end_y = ent.position.y + ent.velocity.y * replay_seconds;
            include_point(bounds, end_x, end_y);
        }
    }

    return bounds;
}

// --- Loading ---

void viewer_load(ViewerState& vs, const std::string& replay_path) {
    ReplayReader reader(replay_path);
    auto events = reader.read_all();

    if (events.empty())
        throw std::runtime_error("empty replay file");

    // Parse header
    const auto& hdr = events[0];
    std::string header_context = "event[0]";
    if (hdr.type != JsonValue::OBJECT)
        throw std::runtime_error(header_context + ": expected object, got " + std::string(json_type_name(hdr.type)));
    if (get_required_string(hdr, "type", header_context) != "header")
        throw std::runtime_error(header_context + ": expected type == 'header'");

    std::string header_scenario_path = get_required_string(hdr, "scenario", header_context);
    const int replay_version = hdr.has("replay_version") ? get_required_int(hdr, "replay_version", header_context) : 1;
    vs.scenario_path = resolve_scenario_path_from_replay(replay_path, header_scenario_path);
    int header_ticks = get_required_int(hdr, "ticks", header_context);
    double header_dt = get_required_number(hdr, "dt", header_context);
    vs.scenario = load_scenario(vs.scenario_path);
    vs.total_ticks = header_ticks > 0 ? header_ticks : vs.scenario.ticks;
    vs.entities_by_id.clear();
    for (const auto& ent : vs.scenario.entities)
        vs.entities_by_id[ent.id] = &ent;

    // Pre-allocate frames
    vs.frames.resize(vs.total_ticks);

    std::vector<std::string> parse_warnings;
    if (vs.scenario.ticks != header_ticks) {
        parse_warnings.push_back("event[0]: header ticks (" + std::to_string(header_ticks) +
                                 ") differs from scenario ticks (" + std::to_string(vs.scenario.ticks) + ")");
    }
    if (std::fabs(vs.scenario.dt - static_cast<float>(header_dt)) > 1e-6f) {
        parse_warnings.push_back("event[0]: header dt (" + std::to_string(header_dt) +
                                 ") differs from scenario dt (" + std::to_string(vs.scenario.dt) + ")");
    }

    // Index events by tick
    for (size_t i = 1; i < events.size(); ++i) {
        const auto& ev = events[i];
        std::string context = "event[" + std::to_string(i) + "]";
        try {
            if (ev.type != JsonValue::OBJECT)
                throw std::runtime_error(context + ": expected object, got " + json_type_name(ev.type));

            int tick = get_required_int(ev, "tick", context);
            if (tick < 0 || tick >= vs.total_ticks) {
                parse_warnings.push_back(context + ": tick out of range (" + std::to_string(tick) + ")");
                continue;
            }

            std::string type = get_required_string(ev, "type", context);

            if (type == "detection") {
                get_required_array(ev, "est_pos", 2, context);
                vs.frames[tick].detections.push_back(ev);
            } else if (type == "track_update") {
                get_required_array(ev, "pos", 2, context);
                get_required_number(ev, "unc", context);
                get_required_number(ev, "conf", context);
                get_required_string(ev, "status", context);
                vs.frames[tick].track_updates.push_back(ev);
            } else if (type == "track_expired") {
                vs.frames[tick].track_expired.push_back(ev);
            } else if (type == "msg_sent" || type == "msg_delivered" || type == "msg_dropped") {
                get_required_int(ev, "sender", context);
                get_required_int(ev, "receiver", context);
                if (type == "msg_sent") get_required_int(ev, "delivery_tick", context);
                vs.frames[tick].messages.push_back(ev);
            } else if (type == "entity_pos") {
                auto pos_arr = get_required_array(ev, "pos", 2, context);
                EntityId eid = static_cast<EntityId>(get_required_int(ev, "entity", context));
                Vec2 pos = {static_cast<float>(pos_arr[0].as_number()),
                            static_cast<float>(pos_arr[1].as_number())};
                vs.frames[tick].entity_positions[eid] = pos;
            } else if (type == "world_hash") {
                vs.frames[tick].world_hash = get_required_string(ev, "hash", context);
            } else if (type == "stats") {
                vs.frames[tick].stats_snapshot = ev;
            } else if (type == "action_resolved") {
                if (replay_version >= 2 && ev.has("rejection_reasons"))
                    get_required_array(ev, "rejection_reasons", 0, context);
                vs.frames[tick].action_resolved.push_back(ev);
            }
        } catch (const std::exception& ex) {
            parse_warnings.push_back(ex.what());
        }
    }

    if (!parse_warnings.empty()) {
        std::fprintf(stderr, "viewer_load: %zu parse warning(s)\n", parse_warnings.size());
        for (const auto& warning : parse_warnings) {
            std::fprintf(stderr, "  - %s\n", warning.c_str());
        }
    }

    // Compute active designations per frame by walking events chronologically
    {
        std::vector<DesignationOverlay> active;
        for (int tick = 0; tick < vs.total_ticks; ++tick) {
            // Remove expired designations
            active.erase(
                std::remove_if(active.begin(), active.end(),
                    [tick](const DesignationOverlay& d) { return tick >= d.expires_tick; }),
                active.end());

            // Process action_resolved events for this tick
            for (const auto& ev : vs.frames[tick].action_resolved) {
                if (!ev.has("allowed")) continue;
                bool allowed = ev["allowed"].as_bool();
                if (!allowed) continue;
                if (!ev.has("action")) continue;
                std::string action = ev["action"].as_string();

                EntityId track_target = 0;
                if (ev.has("track_target"))
                    track_target = static_cast<EntityId>(ev["track_target"].as_number());
                EntityId actor = 0;
                if (ev.has("actor"))
                    actor = static_cast<EntityId>(ev["actor"].as_number());

                if (action == "DESIGNATE_TRACK") {
                    std::string kind = "OBSERVE";
                    if (ev.has("desig_kind"))
                        kind = ev["desig_kind"].as_string();
                    active.push_back({track_target, kind, actor, tick + 30});
                } else if (action == "CLEAR_DESIGNATION") {
                    for (auto it = active.begin(); it != active.end(); ++it) {
                        if (it->track_target == track_target && it->issuer == actor) {
                            active.erase(it);
                            break;
                        }
                    }
                }
            }

            vs.frames[tick].active_designations = active;
        }
    }

    // Compute camera to fit map
    const WorldBounds bounds = compute_world_bounds(vs.scenario);
    const float margin = 20.0f;
    const float min_extent = 1.0f;
    const float default_zoom = 10.0f;
    float screen_w = static_cast<float>(GetScreenWidth());
    float screen_h = static_cast<float>(GetScreenHeight()) - 60.0f; // reserve bottom bar

    if (!bounds.has_points) {
        vs.cam_x = 0.0f;
        vs.cam_y = 0.0f;
        vs.zoom = default_zoom;
        return;
    }

    const float world_w = std::max((bounds.max_x - bounds.min_x) + margin * 2.0f, min_extent);
    const float world_h = std::max((bounds.max_y - bounds.min_y) + margin * 2.0f, min_extent);

    vs.zoom = std::min(screen_w / world_w, screen_h / world_h);
    vs.cam_x = (bounds.min_x + bounds.max_x) / 2.0f;
    vs.cam_y = (bounds.min_y + bounds.max_y) / 2.0f;
}
