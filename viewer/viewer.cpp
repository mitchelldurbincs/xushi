#include "viewer.h"
#include "../src/replay.h"
#include "raylib.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
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

static const std::string& get_required_string(const JsonValue& obj, const std::string& key, const std::string& context) {
    if (!obj.has(key))
        throw std::runtime_error(context + ": missing required key '" + key + "'");
    const auto& v = obj[key];
    if (v.type != JsonValue::STRING)
        throw std::runtime_error(context + ": key '" + key + "' expected string, got " + json_type_name(v.type));
    return v.as_string();
}

static int get_required_int(const JsonValue& obj, const std::string& key, const std::string& context) {
    if (!obj.has(key))
        throw std::runtime_error(context + ": missing required key '" + key + "'");
    const auto& v = obj[key];
    if (v.type != JsonValue::NUMBER)
        throw std::runtime_error(context + ": key '" + key + "' expected number/int, got " + json_type_name(v.type));
    return v.as_int();
}

static double get_required_number(const JsonValue& obj, const std::string& key, const std::string& context) {
    if (!obj.has(key))
        throw std::runtime_error(context + ": missing required key '" + key + "'");
    const auto& v = obj[key];
    if (v.type != JsonValue::NUMBER)
        throw std::runtime_error(context + ": key '" + key + "' expected number, got " + json_type_name(v.type));
    return v.as_number();
}

static const std::vector<JsonValue>& get_required_array(const JsonValue& obj, const std::string& key, size_t min_len,
                                                        const std::string& context) {
    if (!obj.has(key))
        throw std::runtime_error(context + ": missing required key '" + key + "'");
    const auto& v = obj[key];
    if (v.type != JsonValue::ARRAY)
        throw std::runtime_error(context + ": key '" + key + "' expected array, got " + json_type_name(v.type));
    const auto& arr = v.as_array();
    if (arr.size() < min_len)
        throw std::runtime_error(context + ": key '" + key + "' expected array len >= " + std::to_string(min_len));
    return arr;
}

static bool try_get_xy_array(const JsonValue& obj, const std::string& key, float& x, float& y) {
    if (!obj.has(key)) return false;
    const auto& v = obj[key];
    if (v.type != JsonValue::ARRAY) return false;
    const auto& arr = v.as_array();
    if (arr.size() < 2 || arr[0].type != JsonValue::NUMBER || arr[1].type != JsonValue::NUMBER) return false;
    x = static_cast<float>(arr[0].as_number());
    y = static_cast<float>(arr[1].as_number());
    return true;
}

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

    vs.scenario_path = get_required_string(hdr, "scenario", header_context);
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

            const std::string& type = get_required_string(ev, "type", context);

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
                const auto& pos_arr = get_required_array(ev, "pos", 2, context);
                EntityId eid = static_cast<EntityId>(get_required_int(ev, "entity", context));
                Vec2 pos = {static_cast<float>(pos_arr[0].as_number()),
                            static_cast<float>(pos_arr[1].as_number())};
                vs.frames[tick].entity_positions[eid] = pos;
            } else if (type == "world_hash") {
                vs.frames[tick].world_hash = get_required_string(ev, "hash", context);
            } else if (type == "stats") {
                vs.frames[tick].stats_snapshot = ev;
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

// --- Coordinate transform ---

static Vector2 world_to_screen(float wx, float wy, const ViewerState& vs) {
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight()) - 60.0f;
    float sx = sw / 2.0f + (wx - vs.cam_x) * vs.zoom;
    float sy = sh / 2.0f + (wy - vs.cam_y) * vs.zoom;
    return {sx, sy};
}

// --- Update ---

void viewer_update(ViewerState& vs) {
    // Play/pause
    if (IsKeyPressed(KEY_SPACE))
        vs.playing = !vs.playing;

    // Step
    if (IsKeyPressed(KEY_RIGHT) && !vs.playing)
        vs.current_tick = std::min(vs.current_tick + 1, vs.total_ticks - 1);
    if (IsKeyPressed(KEY_LEFT) && !vs.playing)
        vs.current_tick = std::max(vs.current_tick - 1, 0);

    // Speed
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
        vs.playback_speed = std::min(vs.playback_speed * 2.0f, 120.0f);
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
        vs.playback_speed = std::max(vs.playback_speed / 2.0f, 0.5f);

    // Zoom
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        vs.zoom *= (wheel > 0) ? 1.15f : (1.0f / 1.15f);
        vs.zoom = std::max(0.5f, std::min(vs.zoom, 50.0f));
    }

    // Pan (right mouse button or middle button)
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        vs.cam_x -= delta.x / vs.zoom;
        vs.cam_y -= delta.y / vs.zoom;
    }

    // Slider
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());
    float slider_x = 80.0f;
    float slider_w = sw - 160.0f;
    float slider_y = sh - 35.0f;

    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        mouse.y > sh - 60.0f && mouse.x > slider_x && mouse.x < slider_x + slider_w) {
        vs.dragging_slider = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        vs.dragging_slider = false;

    if (vs.dragging_slider) {
        float frac = (mouse.x - slider_x) / slider_w;
        frac = std::max(0.0f, std::min(1.0f, frac));
        vs.current_tick = static_cast<int>(frac * (vs.total_ticks - 1));
        vs.playing = false;
    }

    // Playback advance
    if (vs.playing) {
        vs.tick_accumulator += vs.playback_speed * GetFrameTime();
        int steps = static_cast<int>(vs.tick_accumulator);
        vs.tick_accumulator -= steps;
        vs.current_tick += steps;
        if (vs.current_tick >= vs.total_ticks) {
            vs.current_tick = vs.total_ticks - 1;
            vs.playing = false;
        }
    }
}

// --- Draw ---

static void draw_grid(const ViewerState& vs) {
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight()) - 60.0f;

    // Determine grid spacing based on zoom
    float spacing = 10.0f;
    if (vs.zoom < 2.0f) spacing = 50.0f;
    if (vs.zoom < 0.8f) spacing = 100.0f;

    // World bounds visible on screen
    float world_left = vs.cam_x - sw / (2.0f * vs.zoom);
    float world_right = vs.cam_x + sw / (2.0f * vs.zoom);
    float world_top = vs.cam_y - sh / (2.0f * vs.zoom);
    float world_bottom = vs.cam_y + sh / (2.0f * vs.zoom);

    float start_x = std::floor(world_left / spacing) * spacing;
    float start_y = std::floor(world_top / spacing) * spacing;

    Color grid_color = {40, 40, 40, 255};

    for (float x = start_x; x <= world_right; x += spacing) {
        Vector2 top = world_to_screen(x, world_top, vs);
        Vector2 bot = world_to_screen(x, world_bottom, vs);
        DrawLineV(top, bot, grid_color);
    }
    for (float y = start_y; y <= world_bottom; y += spacing) {
        Vector2 left = world_to_screen(world_left, y, vs);
        Vector2 right = world_to_screen(world_right, y, vs);
        DrawLineV(left, right, grid_color);
    }
}

static void draw_obstacles(const ViewerState& vs) {
    for (const auto& obs : vs.scenario.obstacles) {
        Vector2 tl = world_to_screen(obs.min.x, obs.min.y, vs);
        Vector2 br = world_to_screen(obs.max.x, obs.max.y, vs);
        float w = br.x - tl.x;
        float h = br.y - tl.y;
        DrawRectangle(static_cast<int>(tl.x), static_cast<int>(tl.y),
                      static_cast<int>(w), static_cast<int>(h), {70, 70, 80, 255});
        DrawRectangleLinesEx({tl.x, tl.y, w, h}, 1.0f, {100, 100, 110, 255});
    }
}

static void draw_entities(const ViewerState& vs) {
    float dt = vs.scenario.dt;
    int tick = vs.current_tick;

    // Get entity positions from replay if available for this tick
    const std::map<EntityId, Vec2>* tick_positions = nullptr;
    if (tick >= 0 && tick < static_cast<int>(vs.frames.size()))
        tick_positions = &vs.frames[tick].entity_positions;

    for (const auto& ent : vs.scenario.entities) {
        float wx, wy;
        // Use replay position if available, otherwise extrapolate from constant velocity
        if (tick_positions) {
            auto it = tick_positions->find(ent.id);
            if (it != tick_positions->end()) {
                wx = it->second.x;
                wy = it->second.y;
            } else {
                wx = ent.position.x + ent.velocity.x * dt * (tick + 1);
                wy = ent.position.y + ent.velocity.y * dt * (tick + 1);
            }
        } else {
            wx = ent.position.x + ent.velocity.x * dt * (tick + 1);
            wy = ent.position.y + ent.velocity.y * dt * (tick + 1);
        }
        Vector2 pos = world_to_screen(wx, wy, vs);

        float r = std::max(4.0f, 5.0f * vs.zoom / 3.0f);

        if (ent.role == ScenarioEntity::Role::Drone) {
            DrawCircleV(pos, r, {50, 130, 240, 255});
            DrawCircleLinesV(pos, r, {80, 160, 255, 255});
        } else if (ent.role == ScenarioEntity::Role::Ground) {
            DrawRectangle(static_cast<int>(pos.x - r), static_cast<int>(pos.y - r),
                          static_cast<int>(r * 2), static_cast<int>(r * 2),
                          {50, 180, 100, 255});
        } else if (ent.role == ScenarioEntity::Role::Target) {
            DrawCircleV(pos, r, {220, 60, 60, 255});
            DrawCircleLinesV(pos, r, {255, 100, 100, 255});
        }

        // Label
        const char* role_str = (ent.role == ScenarioEntity::Role::Drone) ? "drone"
                             : (ent.role == ScenarioEntity::Role::Ground) ? "ground"
                             : "target";
        const char* label = TextFormat("%s %u", role_str, ent.id);
        int font_size = 10;
        DrawText(label, static_cast<int>(pos.x - MeasureText(label, font_size) / 2),
                 static_cast<int>(pos.y - r - 14), font_size, {200, 200, 200, 200});
    }
}

static void draw_detections(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];

    float dt = vs.scenario.dt;
    int tick = vs.current_tick;

    int missing_observer_count = 0;

    for (const auto& det : frame.detections) {
        float ex = 0.0f, ey = 0.0f;
        if (!try_get_xy_array(det, "est_pos", ex, ey)) continue;
        Vector2 est_screen = world_to_screen(ex, ey, vs);

        bool draw_los = false;
        Vector2 observer_screen = {0, 0};
        if (det.has("observer")) {
            EntityId observer_id = static_cast<EntityId>(det["observer"].as_int());

            // Use replay position if available, otherwise extrapolate
            auto pos_it = frame.entity_positions.find(observer_id);
            if (pos_it != frame.entity_positions.end()) {
                observer_screen = world_to_screen(pos_it->second.x, pos_it->second.y, vs);
                draw_los = true;
            } else {
                auto it = vs.entities_by_id.find(observer_id);
                if (it != vs.entities_by_id.end() && it->second != nullptr) {
                    const ScenarioEntity* observer = it->second;
                    float wx = observer->position.x + observer->velocity.x * dt * (tick + 1);
                    float wy = observer->position.y + observer->velocity.y * dt * (tick + 1);
                    observer_screen = world_to_screen(wx, wy, vs);
                    draw_los = true;
                }
            }
        }

        // LOS line (only when observer is valid)
        if (draw_los) {
            DrawLineEx(observer_screen, est_screen, 1.5f, {80, 220, 80, 140});
        } else {
            missing_observer_count++;
        }

        // Detection marker
        DrawCircleV(est_screen, 3.0f, {80, 220, 80, 200});
    }

    (void)missing_observer_count;
}

static void draw_tracks(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];

    for (const auto& trk : frame.track_updates) {
        float px = 0.0f, py = 0.0f;
        if (!try_get_xy_array(trk, "pos", px, py)) continue;
        if (!trk.has("unc") || trk["unc"].type != JsonValue::NUMBER) continue;
        if (!trk.has("conf") || trk["conf"].type != JsonValue::NUMBER) continue;
        if (!trk.has("status") || trk["status"].type != JsonValue::STRING) continue;
        float unc = static_cast<float>(trk["unc"].as_number());
        float conf = static_cast<float>(trk["conf"].as_number());
        std::string status = trk["status"].as_string();

        Vector2 pos = world_to_screen(px, py, vs);
        float screen_unc = unc * vs.zoom;

        unsigned char alpha = static_cast<unsigned char>(conf * 200 + 30);

        Color fill, outline;
        if (status == "FRESH") {
            fill = {255, 180, 50, static_cast<unsigned char>(alpha / 3)};
            outline = {255, 200, 80, alpha};
        } else {
            fill = {200, 100, 30, static_cast<unsigned char>(alpha / 4)};
            outline = {200, 120, 50, alpha};
        }

        DrawCircleV(pos, screen_unc, fill);
        DrawCircleLinesV(pos, screen_unc, outline);

        // Cross at center
        DrawLineEx({pos.x - 4, pos.y}, {pos.x + 4, pos.y}, 1.0f, outline);
        DrawLineEx({pos.x, pos.y - 4}, {pos.x, pos.y + 4}, 1.0f, outline);
    }
}

static void draw_messages(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];
    float sh = static_cast<float>(GetScreenHeight());

    int y_offset = 0;
    for (const auto& msg : frame.messages) {
        if (!msg.has("type") || msg["type"].type != JsonValue::STRING) continue;
        std::string type = msg["type"].as_string();
        Color col;
        const char* icon;
        if (type == "msg_sent")      { col = {100, 200, 255, 200}; icon = "MSG SENT"; }
        else if (type == "msg_delivered") { col = {100, 255, 100, 200}; icon = "MSG DELIVERED"; }
        else                              { col = {255, 100, 100, 200}; icon = "MSG DROPPED"; }

        DrawText(icon, 10, static_cast<int>(sh - 80 - y_offset * 16), 10, col);
        y_offset++;
    }

    for (const auto& expired : frame.track_expired) {
        int owner = expired.int_or("owner", -1);
        int target = expired.int_or("target", -1);
        const char* txt = TextFormat("TRACK EXPIRED %d -> %d", owner, target);
        DrawText(txt, 10, static_cast<int>(sh - 80 - y_offset * 16), 10, {255, 180, 120, 210});
        y_offset++;
    }
}

static void draw_ui(const ViewerState& vs) {
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());

    // Bottom bar background
    DrawRectangle(0, static_cast<int>(sh - 60), static_cast<int>(sw), 60, {25, 25, 30, 240});
    DrawLineEx({0, sh - 60}, {sw, sh - 60}, 1.0f, {60, 60, 70, 255});

    // Slider
    float slider_x = 80.0f;
    float slider_w = sw - 160.0f;
    float slider_y = sh - 35.0f;
    float frac = (vs.total_ticks > 1) ? static_cast<float>(vs.current_tick) / (vs.total_ticks - 1) : 0;

    // Track
    DrawRectangle(static_cast<int>(slider_x), static_cast<int>(slider_y - 2),
                  static_cast<int>(slider_w), 4, {60, 60, 70, 255});
    // Filled portion
    DrawRectangle(static_cast<int>(slider_x), static_cast<int>(slider_y - 2),
                  static_cast<int>(slider_w * frac), 4, {80, 160, 255, 255});
    // Handle
    float handle_x = slider_x + slider_w * frac;
    DrawCircle(static_cast<int>(handle_x), static_cast<int>(slider_y), 7, {80, 160, 255, 255});

    // Tick counter
    const char* tick_text = TextFormat("tick %d / %d", vs.current_tick, vs.total_ticks - 1);
    DrawText(tick_text, 10, static_cast<int>(sh - 50), 16, {200, 200, 200, 255});

    // Speed
    const char* speed_text = TextFormat("%.1fx", vs.playback_speed);
    DrawText(speed_text, static_cast<int>(sw - 60), static_cast<int>(sh - 50), 16, {160, 160, 170, 255});

    // Play/pause indicator
    const char* state_text = vs.playing ? "PLAYING" : "PAUSED";
    Color state_col = vs.playing ? Color{100, 255, 100, 200} : Color{255, 200, 80, 200};
    DrawText(state_text, 10, static_cast<int>(sh - 30), 12, state_col);

    // Diagnostics (compact, top-left)
    std::string latest_hash = "-";
    for (int t = std::min(vs.current_tick, vs.total_ticks - 1); t >= 0; --t) {
        if (!vs.frames[t].world_hash.empty()) {
            latest_hash = vs.frames[t].world_hash;
            break;
        }
    }

    int latest_stats_tick = -1;
    for (int t = std::min(vs.current_tick, vs.total_ticks - 1); t >= 0; --t) {
        if (vs.frames[t].stats_snapshot.type == JsonValue::OBJECT) {
            latest_stats_tick = t;
            break;
        }
    }

    int active_tracks = 0;
    int expired_tracks = 0;
    int stats_sent = -1;
    int stats_delivered = -1;
    int stats_dropped = -1;

    if (latest_stats_tick >= 0) {
        const auto& stats = vs.frames[latest_stats_tick].stats_snapshot;
        active_tracks = stats.int_or("tracks_active", 0);
        expired_tracks = stats.int_or("tracks_expired", 0);
        stats_sent = stats.int_or("messages_sent", -1);
        stats_delivered = stats.int_or("messages_delivered", -1);
        stats_dropped = stats.int_or("messages_dropped", -1);
    } else if (vs.current_tick >= 0 && vs.current_tick < static_cast<int>(vs.frames.size())) {
        // Graceful fallback when no stats snapshot is available yet.
        active_tracks = static_cast<int>(vs.frames[vs.current_tick].track_updates.size());
        expired_tracks = static_cast<int>(vs.frames[vs.current_tick].track_expired.size());
    }

    DrawRectangle(10, 28, 260, 58, {25, 25, 30, 200});
    DrawText(TextFormat("diag hash@<=tick: %s", latest_hash.c_str()), 16, 34, 10, {170, 170, 185, 220});
    DrawText(TextFormat("tracks active/expired: %d/%d", active_tracks, expired_tracks),
             16, 47, 10, {170, 170, 185, 220});

    if (latest_stats_tick >= 0) {
        DrawText(TextFormat("stats@%d msg s/d/x: %d/%d/%d",
                 latest_stats_tick, stats_sent, stats_delivered, stats_dropped),
                 16, 60, 10, {170, 170, 185, 220});
    } else {
        DrawText("stats: n/a", 16, 60, 10, {120, 120, 130, 200});
    }

    // Legend (top right)
    int lx = static_cast<int>(sw - 170);
    int ly = 10;
    DrawRectangle(lx - 5, ly - 5, 170, 90, {25, 25, 30, 200});
    DrawCircle(lx + 6, ly + 8, 5, {50, 130, 240, 255});
    DrawText("Drone", lx + 16, ly + 2, 12, {180, 180, 190, 200});
    DrawRectangle(lx + 1, ly + 21, 10, 10, {50, 180, 100, 255});
    DrawText("Ground", lx + 16, ly + 20, 12, {180, 180, 190, 200});
    DrawCircle(lx + 6, ly + 40, 5, {220, 60, 60, 255});
    DrawText("Target", lx + 16, ly + 34, 12, {180, 180, 190, 200});
    DrawCircleLinesV({static_cast<float>(lx + 6), static_cast<float>(ly + 56)}, 6, {255, 200, 80, 200});
    DrawText("Belief track", lx + 16, ly + 50, 12, {180, 180, 190, 200});
    DrawLineEx({static_cast<float>(lx), static_cast<float>(ly + 70)},
               {static_cast<float>(lx + 12), static_cast<float>(ly + 70)}, 1.5f, {80, 220, 80, 140});
    DrawText("LOS / detection", lx + 16, ly + 64, 12, {180, 180, 190, 200});

    // Controls hint
    DrawText("SPACE: play/pause  ARROWS: step  +/-: speed  SCROLL: zoom  RIGHT-DRAG: pan",
             10, 10, 10, {120, 120, 130, 160});
}

void viewer_draw(const ViewerState& vs) {
    ClearBackground({20, 20, 25, 255});
    draw_grid(vs);
    draw_obstacles(vs);
    draw_detections(vs);
    draw_tracks(vs);
    draw_entities(vs);
    draw_messages(vs);
    draw_ui(vs);
}
