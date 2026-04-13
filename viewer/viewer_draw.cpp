#include "viewer.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// --- Coordinate transform ---

static Vector2 world_to_screen(float wx, float wy, const ViewerState& vs) {
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight()) - 60.0f;
    float sx = sw / 2.0f + (wx - vs.cam_x) * vs.zoom;
    float sy = sh / 2.0f + (wy - vs.cam_y) * vs.zoom;
    return {sx, sy};
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

// --- Grid ---

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

// --- Obstacles ---

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

// --- Sensor range circles ---

static void draw_sensor_ranges(const ViewerState& vs) {
    int tick = vs.current_tick;
    const std::map<EntityId, Vec2>* tick_positions = nullptr;
    if (tick >= 0 && tick < static_cast<int>(vs.frames.size()))
        tick_positions = &vs.frames[tick].entity_positions;

    float range = vs.scenario.max_sensor_range;

    for (const auto& ent : vs.scenario.entities) {
        if (!ent.can_sense) continue;

        float wx, wy;
        if (tick_positions) {
            auto it = tick_positions->find(ent.id);
            if (it != tick_positions->end()) {
                wx = it->second.x;
                wy = it->second.y;
            } else {
                wx = ent.position.x + ent.velocity.x * vs.scenario.dt * (tick + 1);
                wy = ent.position.y + ent.velocity.y * vs.scenario.dt * (tick + 1);
            }
        } else {
            wx = ent.position.x + ent.velocity.x * vs.scenario.dt * (tick + 1);
            wy = ent.position.y + ent.velocity.y * vs.scenario.dt * (tick + 1);
        }

        Vector2 pos = world_to_screen(wx, wy, vs);
        float screen_range = range * vs.zoom;

        DrawCircleV(pos, screen_range, {50, 130, 240, 20});
        DrawCircleLinesV(pos, screen_range, {50, 130, 240, 60});
    }
}

// --- Waypoint paths ---

static void draw_waypoint_paths(const ViewerState& vs) {
    for (const auto& ent : vs.scenario.entities) {
        if (ent.waypoints.empty()) continue;

        int n = static_cast<int>(ent.waypoints.size());

        // Choose color based on entity type
        Color path_color;
        if (ent.is_observable)
            path_color = {220, 60, 60, 80};
        else if (ent.can_sense)
            path_color = {50, 130, 240, 80};
        else
            path_color = {150, 150, 150, 80};

        Color dot_color = {path_color.r, path_color.g, path_color.b, 140};

        // Draw path segments
        for (int i = 0; i < n; ++i) {
            Vector2 from = world_to_screen(ent.waypoints[i].x, ent.waypoints[i].y, vs);

            // Draw waypoint dot
            DrawCircleV(from, 3.0f, dot_color);

            // Check for branch point
            auto bp_it = ent.branch_points.find(i);
            if (bp_it != ent.branch_points.end() && !bp_it->second.empty()) {
                // Draw lines to all branch successors
                for (int succ : bp_it->second) {
                    if (succ >= 0 && succ < n) {
                        Vector2 to = world_to_screen(ent.waypoints[succ].x, ent.waypoints[succ].y, vs);
                        DrawLineEx(from, to, 1.0f, path_color);
                    }
                }
                // Draw a small diamond to mark the branch point
                DrawCircleLinesV(from, 5.0f, {255, 200, 80, 120});
            } else {
                // Normal sequential segment
                int next = i + 1;
                if (next < n) {
                    Vector2 to = world_to_screen(ent.waypoints[next].x, ent.waypoints[next].y, vs);
                    DrawLineEx(from, to, 1.0f, path_color);
                } else if (ent.waypoint_mode == ScenarioEntity::WaypointMode::Loop) {
                    // Closing segment back to waypoint 0
                    Vector2 to = world_to_screen(ent.waypoints[0].x, ent.waypoints[0].y, vs);
                    DrawLineEx(from, to, 1.0f, {path_color.r, path_color.g, path_color.b, 50});
                }
            }
        }

        // Label first waypoint with index
        Vector2 wp0 = world_to_screen(ent.waypoints[0].x, ent.waypoints[0].y, vs);
        DrawText("wp0", static_cast<int>(wp0.x + 5), static_cast<int>(wp0.y - 10), 8, dot_color);
    }
}

// --- Entities ---

static void draw_entities(const ViewerState& vs) {
    float dt = vs.scenario.dt;
    int tick = vs.current_tick;

    // Get entity positions and vitality from replay if available for this tick
    const std::map<EntityId, Vec2>* tick_positions = nullptr;
    const std::map<EntityId, int>* tick_vitality = nullptr;
    if (tick >= 0 && tick < static_cast<int>(vs.frames.size())) {
        tick_positions = &vs.frames[tick].entity_positions;
        tick_vitality = &vs.frames[tick].entity_vitality;
    }

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

        // Check vitality
        int current_vitality = ent.vitality;
        int max_vitality = ent.max_vitality;
        if (tick_vitality) {
            auto vit_it = tick_vitality->find(ent.id);
            if (vit_it != tick_vitality->end())
                current_vitality = vit_it->second;
        }
        bool is_dead = current_vitality <= 0 && max_vitality > 0;

        // Color by capability: blue=sensor, green=tracker, red=observable, purple=multi
        Color fill, outline;
        int cap_count = (int)ent.can_sense + (int)ent.can_track + (int)ent.is_observable;
        if (cap_count > 1) {
            fill = {180, 100, 220, 255};    // purple: multi-capability
            outline = {210, 140, 255, 255};
        } else if (ent.can_sense) {
            fill = {50, 130, 240, 255};     // blue: sensor
            outline = {80, 160, 255, 255};
        } else if (ent.can_track) {
            fill = {50, 180, 100, 255};     // green: tracker
            outline = {80, 210, 130, 255};
        } else if (ent.is_observable) {
            fill = {220, 60, 60, 255};      // red: observable
            outline = {255, 100, 100, 255};
        } else {
            fill = {150, 150, 150, 255};    // gray: no capabilities
            outline = {180, 180, 180, 255};
        }

        // Dim dead entities
        if (is_dead) {
            fill = {80, 80, 80, 120};
            outline = {120, 120, 120, 120};
            // Draw X over dead entity
            DrawLineEx({pos.x - r, pos.y - r}, {pos.x + r, pos.y + r}, 2.0f, {255, 60, 60, 180});
            DrawLineEx({pos.x + r, pos.y - r}, {pos.x - r, pos.y + r}, 2.0f, {255, 60, 60, 180});
        }

        DrawCircleV(pos, r, fill);
        DrawCircleLinesV(pos, r, outline);

        // Health bar (only for entities with vitality tracking)
        if (max_vitality > 0 && !is_dead) {
            float bar_w = r * 4.0f;
            float bar_h = 3.0f;
            float bar_x = pos.x - bar_w / 2.0f;
            float bar_y = pos.y + r + 4.0f;
            float frac = static_cast<float>(current_vitality) / static_cast<float>(max_vitality);
            frac = std::max(0.0f, std::min(1.0f, frac));

            // Background
            DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                          static_cast<int>(bar_w), static_cast<int>(bar_h), {40, 40, 40, 200});
            // Fill: green->yellow->red based on health
            Color bar_col;
            if (frac > 0.5f)
                bar_col = {50, 200, 80, 230};
            else if (frac > 0.25f)
                bar_col = {240, 200, 50, 230};
            else
                bar_col = {240, 60, 60, 230};
            DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                          static_cast<int>(bar_w * frac), static_cast<int>(bar_h), bar_col);
        }

        // Label
        const char* label = is_dead
            ? TextFormat("%s %u [DEAD]", ent.role_name.c_str(), ent.id)
            : TextFormat("%s %u", ent.role_name.c_str(), ent.id);
        int font_size = 10;
        Color label_col = is_dead ? Color{150, 60, 60, 180} : Color{200, 200, 200, 200};
        DrawText(label, static_cast<int>(pos.x - MeasureText(label, font_size) / 2),
                 static_cast<int>(pos.y - r - 14), font_size, label_col);
    }
}

// --- Detections ---

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

// --- Tracks ---

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

// --- Designations ---

static Color designation_color(const std::string& kind) {
    if (kind == "OBSERVE")          return {80, 200, 240, 200};   // cyan
    if (kind == "VERIFY")           return {240, 220, 60, 200};   // yellow
    if (kind == "ENGAGE")           return {240, 60, 60, 200};    // red
    if (kind == "MAINTAIN_CUSTODY") return {60, 200, 100, 200};   // green
    if (kind == "BDA")              return {200, 80, 240, 200};   // magenta
    return {180, 180, 180, 200};                                   // gray fallback
}

static void draw_designations(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];

    for (const auto& desig : frame.active_designations) {
        // Find matching track position from track_updates
        float px = 0.0f, py = 0.0f;
        bool found = false;
        for (const auto& trk : frame.track_updates) {
            if (!trk.has("target")) continue;
            EntityId tid = static_cast<EntityId>(trk["target"].as_number());
            if (tid == desig.track_target) {
                found = try_get_xy_array(trk, "pos", px, py);
                break;
            }
        }
        if (!found) continue;

        Vector2 pos = world_to_screen(px, py, vs);
        Color col = designation_color(desig.kind);

        // Diamond shape around track
        float sz = 10.0f;
        DrawLineEx({pos.x, pos.y - sz}, {pos.x + sz, pos.y}, 1.5f, col);
        DrawLineEx({pos.x + sz, pos.y}, {pos.x, pos.y + sz}, 1.5f, col);
        DrawLineEx({pos.x, pos.y + sz}, {pos.x - sz, pos.y}, 1.5f, col);
        DrawLineEx({pos.x - sz, pos.y}, {pos.x, pos.y - sz}, 1.5f, col);

        // Label
        DrawText(desig.kind.c_str(),
                 static_cast<int>(pos.x + sz + 3),
                 static_cast<int>(pos.y - 5),
                 8, col);
    }
}

// --- Combat effects ---

static void draw_combat_effects(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];

    for (const auto& eff : frame.effect_resolved) {
        if (!eff.has("track_target") || !eff.has("actor")) continue;
        EntityId target_id = static_cast<EntityId>(eff["track_target"].as_int());
        EntityId actor_id = static_cast<EntityId>(eff["actor"].as_int());
        bool hit = eff.has("hit") && eff["hit"].as_bool();

        // Find target position
        auto tgt_it = frame.entity_positions.find(target_id);
        auto act_it = frame.entity_positions.find(actor_id);
        if (tgt_it == frame.entity_positions.end()) continue;

        Vector2 tgt_screen = world_to_screen(tgt_it->second.x, tgt_it->second.y, vs);

        if (hit) {
            // Red burst around target
            float burst_r = std::max(10.0f, 12.0f * vs.zoom / 3.0f);
            DrawCircleLinesV(tgt_screen, burst_r, {255, 50, 50, 200});
            DrawCircleLinesV(tgt_screen, burst_r * 0.6f, {255, 100, 50, 160});

            // Damage text
            if (eff.has("vitality_delta")) {
                int delta = eff["vitality_delta"].as_int();
                const char* dmg_text = TextFormat("%d", delta);
                DrawText(dmg_text,
                         static_cast<int>(tgt_screen.x + 10),
                         static_cast<int>(tgt_screen.y - 16),
                         12, {255, 80, 80, 230});
            }
        } else {
            // Miss indicator: gray "MISS" text
            DrawText("MISS",
                     static_cast<int>(tgt_screen.x + 10),
                     static_cast<int>(tgt_screen.y - 16),
                     10, {180, 180, 180, 160});
        }

        // Draw firing line from actor to target
        if (act_it != frame.entity_positions.end()) {
            Vector2 act_screen = world_to_screen(act_it->second.x, act_it->second.y, vs);
            Color line_col = hit ? Color{255, 80, 50, 140} : Color{180, 180, 180, 80};
            DrawLineEx(act_screen, tgt_screen, 2.0f, line_col);
        }
    }
}

// --- Messages & expired tracks ---

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

// --- HUD / UI overlay ---

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
    int lx = static_cast<int>(sw - 190);
    int ly = 10;
    DrawRectangle(lx - 5, ly - 5, 190, 138, {25, 25, 30, 200});
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
    DrawCircleLinesV({static_cast<float>(lx + 6), static_cast<float>(ly + 86)}, 6, {50, 130, 240, 60});
    DrawText(vs.show_sensor_ranges ? "Sensor range [R]" : "Sensor range [R] OFF",
             lx + 16, ly + 80, 12,
             vs.show_sensor_ranges ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});
    DrawLineEx({static_cast<float>(lx), static_cast<float>(ly + 100)},
               {static_cast<float>(lx + 12), static_cast<float>(ly + 100)}, 1.0f, {220, 60, 60, 80});
    DrawText(vs.show_waypoint_paths ? "Waypoint path [W]" : "Waypoint path [W] OFF",
             lx + 16, ly + 94, 12,
             vs.show_waypoint_paths ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});
    // Diamond icon for designation
    {
        float dx = static_cast<float>(lx + 6);
        float dy = static_cast<float>(ly + 114);
        DrawLineEx({dx, dy - 5}, {dx + 5, dy}, 1.0f, {80, 200, 240, 200});
        DrawLineEx({dx + 5, dy}, {dx, dy + 5}, 1.0f, {80, 200, 240, 200});
        DrawLineEx({dx, dy + 5}, {dx - 5, dy}, 1.0f, {80, 200, 240, 200});
        DrawLineEx({dx - 5, dy}, {dx, dy - 5}, 1.0f, {80, 200, 240, 200});
    }
    DrawText(vs.show_designations ? "Designation [D]" : "Designation [D] OFF",
             lx + 16, ly + 108, 12,
             vs.show_designations ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});

    // Controls hint
    DrawText("SPACE: play/pause  ARROWS: step  +/-: speed  SCROLL: zoom  RIGHT-DRAG: pan  R: ranges  W: waypoints  D: desig",
             10, 10, 10, {120, 120, 130, 160});
}

// --- Main draw entry point ---

void viewer_draw(const ViewerState& vs) {
    ClearBackground({20, 20, 25, 255});
    draw_grid(vs);
    draw_obstacles(vs);
    if (vs.show_sensor_ranges)
        draw_sensor_ranges(vs);
    if (vs.show_waypoint_paths)
        draw_waypoint_paths(vs);
    draw_detections(vs);
    draw_tracks(vs);
    if (vs.show_designations)
        draw_designations(vs);
    draw_entities(vs);
    draw_combat_effects(vs);
    draw_messages(vs);
    draw_ui(vs);
}
