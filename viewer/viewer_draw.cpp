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

static bool try_get_entity_world_pos(const ViewerState& vs, EntityId id, int tick, float& x, float& y) {
    // Return false only when the entity id is unknown to the scenario.
    auto ent_it = vs.entities_by_id.find(id);
    if (ent_it == vs.entities_by_id.end() || ent_it->second == nullptr) return false;

    // Use replay entity position when tick/frame data is valid and available.
    if (tick >= 0 && tick < static_cast<int>(vs.frames.size())) {
        const auto& tick_positions = vs.frames[tick].entity_positions;
        auto pos_it = tick_positions.find(id);
        if (pos_it != tick_positions.end()) {
            x = pos_it->second.x;
            y = pos_it->second.y;
            return true;
        }
    }

    // Fall back to scenario initial position plus constant-velocity extrapolation.
    const ScenarioEntity* ent = ent_it->second;
    x = ent->position.x + ent->velocity.x * vs.scenario.dt * (tick + 1);
    y = ent->position.y + ent->velocity.y * vs.scenario.dt * (tick + 1);
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

    Color cross_color = {50, 50, 60, 255};

    for (float x = start_x; x <= world_right; x += spacing) {
        for (float y = start_y; y <= world_bottom; y += spacing) {
            Vector2 pt = world_to_screen(x, y, vs);
            DrawLineEx({pt.x - 2, pt.y}, {pt.x + 2, pt.y}, 1.0f, cross_color);
            DrawLineEx({pt.x, pt.y - 2}, {pt.x, pt.y + 2}, 1.0f, cross_color);
        }
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
                      static_cast<int>(w), static_cast<int>(h), {15, 15, 20, 200});
        DrawRectangleLinesEx({tl.x, tl.y, w, h}, 1.0f, {60, 60, 70, 255});
    }
}

// --- Sensor range circles ---

static void draw_sensor_ranges(const ViewerState& vs) {
    int tick = vs.current_tick;
    float range = vs.scenario.max_sensor_range;

    for (const auto& ent : vs.scenario.entities) {
        if (!ent.can_sense) continue;

        float wx, wy;
        if (!try_get_entity_world_pos(vs, ent.id, tick, wx, wy)) continue;

        Vector2 pos = world_to_screen(wx, wy, vs);
        float screen_range = range * vs.zoom;

        DrawCircleLinesV(pos, screen_range, {0, 255, 255, 100});
        // Cardinal ticks
        float tick_len = 6.0f;
        DrawLineEx({pos.x - screen_range - tick_len, pos.y}, {pos.x - screen_range + tick_len, pos.y}, 1.5f, {0, 255, 255, 200});
        DrawLineEx({pos.x + screen_range - tick_len, pos.y}, {pos.x + screen_range + tick_len, pos.y}, 1.5f, {0, 255, 255, 200});
        DrawLineEx({pos.x, pos.y - screen_range - tick_len}, {pos.x, pos.y - screen_range + tick_len}, 1.5f, {0, 255, 255, 200});
        DrawLineEx({pos.x, pos.y + screen_range - tick_len}, {pos.x, pos.y + screen_range + tick_len}, 1.5f, {0, 255, 255, 200});
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
            path_color = {255, 60, 60, 150};
        else if (ent.can_sense)
            path_color = {0, 255, 255, 150};
        else
            path_color = {150, 150, 150, 150};

        Color dot_color = {path_color.r, path_color.g, path_color.b, 200};

        // Draw path segments
        for (int i = 0; i < n; ++i) {
            Vector2 from = world_to_screen(ent.waypoints[i].x, ent.waypoints[i].y, vs);

            // Draw waypoint square instead of dot
            DrawRectangleV({from.x - 2, from.y - 2}, {4, 4}, dot_color);

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
                DrawPoly(from, 4, 5.0f, 0.0f, {255, 200, 80, 120});
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
        DrawText("[WP0]", static_cast<int>(wp0.x + 5), static_cast<int>(wp0.y - 10), 8, dot_color);
    }
}

// --- Entities ---

static void draw_entities(const ViewerState& vs) {
    int tick = vs.current_tick;

    for (const auto& ent : vs.scenario.entities) {
        float wx, wy;
        if (!try_get_entity_world_pos(vs, ent.id, tick, wx, wy)) continue;
        Vector2 pos = world_to_screen(wx, wy, vs);

        float r = std::max(4.0f, 5.0f * vs.zoom / 3.0f);

        Color color;
        int cap_count = (int)ent.can_sense + (int)ent.can_track + (int)ent.is_observable;
        if (cap_count > 1) {
            color = {255, 0, 255, 255};    // magenta: multi-capability
        } else if (ent.can_sense) {
            color = {0, 255, 255, 255};     // cyan: sensor
        } else if (ent.can_track) {
            color = {50, 255, 100, 255};     // lime: tracker
        } else if (ent.is_observable) {
            color = {255, 60, 60, 255};      // red: observable
        } else {
            color = {180, 180, 180, 255};    // gray: no capabilities
        }

        // Draw shapes instead of filled circles
        if (ent.is_observable && !ent.can_sense && !ent.can_track) {
            // Diamond
            DrawLineEx({pos.x, pos.y - r}, {pos.x + r, pos.y}, 1.5f, color);
            DrawLineEx({pos.x + r, pos.y}, {pos.x, pos.y + r}, 1.5f, color);
            DrawLineEx({pos.x, pos.y + r}, {pos.x - r, pos.y}, 1.5f, color);
            DrawLineEx({pos.x - r, pos.y}, {pos.x, pos.y - r}, 1.5f, color);
        } else {
            // Square for sensors/trackers
            DrawRectangleLinesEx({pos.x - r, pos.y - r, r * 2.0f, r * 2.0f}, 1.5f, color);
        }

        // Label
        const char* label = TextFormat("[%s-%u]", ent.role_name.c_str(), ent.id);
        int font_size = 10;
        DrawText(label, static_cast<int>(pos.x - MeasureText(label, font_size) / 2),
                 static_cast<int>(pos.y - r - 14), font_size, {200, 200, 200, 200});
    }
}

// --- Detections ---

static void draw_detections(const ViewerState& vs) {
    if (vs.current_tick < 0 || vs.current_tick >= static_cast<int>(vs.frames.size()))
        return;

    const auto& frame = vs.frames[vs.current_tick];

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
            float wx = 0.0f;
            float wy = 0.0f;
            if (try_get_entity_world_pos(vs, observer_id, tick, wx, wy)) {
                observer_screen = world_to_screen(wx, wy, vs);
                draw_los = true;
            }
        }

        // LOS line (only when observer is valid)
        if (draw_los) {
            DrawLineEx(observer_screen, est_screen, 1.0f, {0, 255, 255, 80});
        } else {
            missing_observer_count++;
        }

        // Detection marker (cross)
        DrawLineEx({est_screen.x - 3, est_screen.y}, {est_screen.x + 3, est_screen.y}, 1.0f, {0, 255, 255, 200});
        DrawLineEx({est_screen.x, est_screen.y - 3}, {est_screen.x, est_screen.y + 3}, 1.0f, {0, 255, 255, 200});
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

        Color color;
        if (status == "FRESH") {
            color = {255, 200, 0, alpha};
        } else {
            color = {200, 120, 0, alpha};
        }

        // Uncertainty brackets instead of circle
        float s = screen_unc;
        float cap = std::min(s * 0.3f, 10.0f); // Bracket size
        
        // Top-left
        DrawLineEx({pos.x - s, pos.y - s}, {pos.x - s + cap, pos.y - s}, 1.5f, color);
        DrawLineEx({pos.x - s, pos.y - s}, {pos.x - s, pos.y - s + cap}, 1.5f, color);
        // Top-right
        DrawLineEx({pos.x + s, pos.y - s}, {pos.x + s - cap, pos.y - s}, 1.5f, color);
        DrawLineEx({pos.x + s, pos.y - s}, {pos.x + s, pos.y - s + cap}, 1.5f, color);
        // Bottom-left
        DrawLineEx({pos.x - s, pos.y + s}, {pos.x - s + cap, pos.y + s}, 1.5f, color);
        DrawLineEx({pos.x - s, pos.y + s}, {pos.x - s, pos.y + s - cap}, 1.5f, color);
        // Bottom-right
        DrawLineEx({pos.x + s, pos.y + s}, {pos.x + s - cap, pos.y + s}, 1.5f, color);
        DrawLineEx({pos.x + s, pos.y + s}, {pos.x + s, pos.y + s - cap}, 1.5f, color);

        // Cross at center
        DrawLineEx({pos.x - 3, pos.y}, {pos.x + 3, pos.y}, 1.0f, color);
        DrawLineEx({pos.x, pos.y - 3}, {pos.x, pos.y + 3}, 1.0f, color);
    }
}

// --- Designations ---

static Color designation_color(const std::string& kind) {
    if (kind == "OBSERVE")          return {0, 255, 255, 200};   // cyan
    if (kind == "VERIFY")           return {255, 200, 0, 200};   // yellow
    if (kind == "ENGAGE")           return {255, 60, 60, 200};    // red
    if (kind == "MAINTAIN_CUSTODY") return {50, 255, 100, 200};   // green
    if (kind == "BDA")              return {255, 0, 255, 200};   // magenta
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

        // Sharp diamond brackets
        float sz = 12.0f;
        float cap = 4.0f;
        // Top point
        DrawLineEx({pos.x, pos.y - sz}, {pos.x + cap, pos.y - sz + cap}, 1.5f, col);
        DrawLineEx({pos.x, pos.y - sz}, {pos.x - cap, pos.y - sz + cap}, 1.5f, col);
        // Bottom point
        DrawLineEx({pos.x, pos.y + sz}, {pos.x + cap, pos.y + sz - cap}, 1.5f, col);
        DrawLineEx({pos.x, pos.y + sz}, {pos.x - cap, pos.y + sz - cap}, 1.5f, col);
        // Left point
        DrawLineEx({pos.x - sz, pos.y}, {pos.x - sz + cap, pos.y - cap}, 1.5f, col);
        DrawLineEx({pos.x - sz, pos.y}, {pos.x - sz + cap, pos.y + cap}, 1.5f, col);
        // Right point
        DrawLineEx({pos.x + sz, pos.y}, {pos.x + sz - cap, pos.y - cap}, 1.5f, col);
        DrawLineEx({pos.x + sz, pos.y}, {pos.x + sz - cap, pos.y + cap}, 1.5f, col);

        // Label
        std::string label = "[" + desig.kind + "]";
        DrawText(label.c_str(),
                 static_cast<int>(pos.x + sz + 3),
                 static_cast<int>(pos.y - 5),
                 10, col);
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
        if (type == "msg_sent")      { col = {0, 255, 255, 200}; icon = "// MSG_SENT"; }
        else if (type == "msg_delivered") { col = {50, 255, 100, 200}; icon = "// MSG_DELIVERED"; }
        else                              { col = {255, 60, 60, 200}; icon = "// MSG_DROPPED"; }

        DrawText(icon, 10, static_cast<int>(sh - 80 - y_offset * 16), 10, col);
        y_offset++;
    }

    for (const auto& expired : frame.track_expired) {
        int owner = expired.int_or("owner", -1);
        int target = expired.int_or("target", -1);
        const char* txt = TextFormat("// TRK_EXPIRED [OWN:%d TGT:%d]", owner, target);
        DrawText(txt, 10, static_cast<int>(sh - 80 - y_offset * 16), 10, {255, 200, 0, 210});
        y_offset++;
    }
}

// --- HUD / UI overlay ---

static void draw_panel(float x, float y, float w, float h) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), {10, 10, 15, 200});
    DrawRectangleLinesEx({x, y, w, h}, 1.0f, {40, 40, 50, 255});
    // Corner accents
    float c = 6.0f;
    Color accent = {100, 100, 120, 255};
    DrawLineEx({x, y}, {x + c, y}, 2.0f, accent);
    DrawLineEx({x, y}, {x, y + c}, 2.0f, accent);
    DrawLineEx({x + w - c, y}, {x + w, y}, 2.0f, accent);
    DrawLineEx({x + w, y}, {x + w, y + c}, 2.0f, accent);
    DrawLineEx({x, y + h - c}, {x, y + h}, 2.0f, accent);
    DrawLineEx({x, y + h}, {x + c, y + h}, 2.0f, accent);
    DrawLineEx({x + w - c, y + h}, {x + w, y + h}, 2.0f, accent);
    DrawLineEx({x + w, y + h - c}, {x + w, y + h}, 2.0f, accent);
}

static void draw_ui(const ViewerState& vs) {
    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());

    // Bottom bar background
    DrawRectangle(0, static_cast<int>(sh - 60), static_cast<int>(sw), 60, {10, 10, 12, 240});
    DrawLineEx({0, sh - 60}, {sw, sh - 60}, 1.0f, {40, 40, 50, 255});

    // Slider
    float slider_x = 90.0f;
    float slider_w = sw - 180.0f;
    float slider_y = sh - 30.0f;
    float frac = (vs.total_ticks > 1) ? static_cast<float>(vs.current_tick) / (vs.total_ticks - 1) : 0;

    // Track
    DrawLineEx({slider_x, slider_y}, {slider_x + slider_w, slider_y}, 1.0f, {60, 60, 70, 255});
    
    // Filled portion
    DrawLineEx({slider_x, slider_y}, {slider_x + slider_w * frac, slider_y}, 1.0f, {0, 255, 255, 255});
    
    // Handle (Caret)
    float handle_x = slider_x + slider_w * frac;
    DrawLineEx({handle_x, slider_y - 8}, {handle_x, slider_y + 8}, 2.0f, {0, 255, 255, 255});
    DrawLineEx({handle_x - 4, slider_y}, {handle_x, slider_y - 4}, 1.5f, {0, 255, 255, 255});
    DrawLineEx({handle_x + 4, slider_y}, {handle_x, slider_y - 4}, 1.5f, {0, 255, 255, 255});
    DrawLineEx({handle_x - 4, slider_y}, {handle_x, slider_y + 4}, 1.5f, {0, 255, 255, 255});
    DrawLineEx({handle_x + 4, slider_y}, {handle_x, slider_y + 4}, 1.5f, {0, 255, 255, 255});

    // Tick counter
    const char* tick_text = TextFormat("[ TICK %d / %d ]", vs.current_tick, vs.total_ticks - 1);
    DrawText(tick_text, 10, static_cast<int>(sh - 40), 10, {200, 200, 200, 255});

    // Speed
    const char* speed_text = TextFormat("[ %.1fx ]", vs.playback_speed);
    DrawText(speed_text, static_cast<int>(sw - 80), static_cast<int>(sh - 40), 10, {160, 160, 170, 255});

    // Play/pause indicator
    const char* state_text = vs.playing ? "▶ PLAYING" : "⏸ PAUSED";
    Color state_col = vs.playing ? Color{50, 255, 100, 200} : Color{255, 200, 0, 200};
    DrawText(state_text, 10, static_cast<int>(sh - 20), 10, state_col);

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

    draw_panel(10, 28, 280, 70);
    DrawText("[ DIAGNOSTICS ]", 16, 34, 10, {0, 255, 255, 200});
    DrawText(TextFormat("HASH : %s", latest_hash.c_str()), 16, 50, 10, {170, 170, 185, 220});
    DrawText(TextFormat("TRK  : %d ACT / %d EXP", active_tracks, expired_tracks),
             16, 63, 10, {170, 170, 185, 220});

    if (latest_stats_tick >= 0) {
        DrawText(TextFormat("MSG  : %d S / %d D / %d X",
                 stats_sent, stats_delivered, stats_dropped),
                 16, 76, 10, {170, 170, 185, 220});
    } else {
        DrawText("MSG  : N/A", 16, 76, 10, {120, 120, 130, 200});
    }

    // Legend (top right)
    int lx = static_cast<int>(sw - 200);
    int ly = 10;
    draw_panel(lx - 5, ly - 5, 200, 160);
    DrawText("[ LEGEND ]", lx + 16, ly + 2, 10, {0, 255, 255, 200});
    
    DrawRectangleLinesEx({static_cast<float>(lx + 2), static_cast<float>(ly + 18), 8, 8}, 1.5f, {0, 255, 255, 255});
    DrawText("SENSOR", lx + 16, ly + 18, 10, {180, 180, 190, 200});
    
    DrawRectangleLinesEx({static_cast<float>(lx + 2), static_cast<float>(ly + 34), 8, 8}, 1.5f, {50, 255, 100, 255});
    DrawText("TRACKER", lx + 16, ly + 34, 10, {180, 180, 190, 200});
    
    DrawLineEx({static_cast<float>(lx + 6), static_cast<float>(ly + 48)}, {static_cast<float>(lx + 10), static_cast<float>(ly + 52)}, 1.5f, {255, 60, 60, 255});
    DrawLineEx({static_cast<float>(lx + 10), static_cast<float>(ly + 52)}, {static_cast<float>(lx + 6), static_cast<float>(ly + 56)}, 1.5f, {255, 60, 60, 255});
    DrawLineEx({static_cast<float>(lx + 6), static_cast<float>(ly + 56)}, {static_cast<float>(lx + 2), static_cast<float>(ly + 52)}, 1.5f, {255, 60, 60, 255});
    DrawLineEx({static_cast<float>(lx + 2), static_cast<float>(ly + 52)}, {static_cast<float>(lx + 6), static_cast<float>(ly + 48)}, 1.5f, {255, 60, 60, 255});
    DrawText("TARGET", lx + 16, ly + 50, 10, {180, 180, 190, 200});
    
    DrawLineEx({static_cast<float>(lx + 2), static_cast<float>(ly + 66)}, {static_cast<float>(lx + 5), static_cast<float>(ly + 66)}, 1.5f, {255, 200, 0, 200});
    DrawLineEx({static_cast<float>(lx + 2), static_cast<float>(ly + 66)}, {static_cast<float>(lx + 2), static_cast<float>(ly + 69)}, 1.5f, {255, 200, 0, 200});
    DrawLineEx({static_cast<float>(lx + 10), static_cast<float>(ly + 74)}, {static_cast<float>(lx + 7), static_cast<float>(ly + 74)}, 1.5f, {255, 200, 0, 200});
    DrawLineEx({static_cast<float>(lx + 10), static_cast<float>(ly + 74)}, {static_cast<float>(lx + 10), static_cast<float>(ly + 71)}, 1.5f, {255, 200, 0, 200});
    DrawText("BELIEF TRACK", lx + 16, ly + 66, 10, {180, 180, 190, 200});
    
    DrawLineEx({static_cast<float>(lx), static_cast<float>(ly + 86)}, {static_cast<float>(lx + 12), static_cast<float>(ly + 86)}, 1.5f, {0, 255, 255, 80});
    DrawText("LOS / DETECTION", lx + 16, ly + 82, 10, {180, 180, 190, 200});
    
    DrawCircleLinesV({static_cast<float>(lx + 6), static_cast<float>(ly + 102)}, 6, {0, 255, 255, 60});
    DrawText(vs.show_sensor_ranges ? "SENSOR RANGE [R]" : "SENSOR RANGE [R] OFF",
             lx + 16, ly + 98, 10,
             vs.show_sensor_ranges ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});
             
    DrawLineEx({static_cast<float>(lx), static_cast<float>(ly + 118)}, {static_cast<float>(lx + 12), static_cast<float>(ly + 118)}, 1.0f, {0, 255, 255, 150});
    DrawText(vs.show_waypoint_paths ? "WAYPOINT PATH [W]" : "WAYPOINT PATH [W] OFF",
             lx + 16, ly + 114, 10,
             vs.show_waypoint_paths ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});
             
    // Diamond icon for designation
    {
        float dx = static_cast<float>(lx + 6);
        float dy = static_cast<float>(ly + 134);
        DrawLineEx({dx, dy - 4}, {dx + 4, dy}, 1.0f, {0, 255, 255, 200});
        DrawLineEx({dx + 4, dy}, {dx, dy + 4}, 1.0f, {0, 255, 255, 200});
        DrawLineEx({dx, dy + 4}, {dx - 4, dy}, 1.0f, {0, 255, 255, 200});
        DrawLineEx({dx - 4, dy}, {dx, dy - 4}, 1.0f, {0, 255, 255, 200});
    }
    DrawText(vs.show_designations ? "DESIGNATION [D]" : "DESIGNATION [D] OFF",
             lx + 16, ly + 130, 10,
             vs.show_designations ? Color{180, 180, 190, 200} : Color{100, 100, 110, 150});

    // Controls hint
    DrawText("[ SPACE: PLAY/PAUSE | ARROWS: STEP | +/-: SPEED | SCROLL: ZOOM | RIGHT-DRAG: PAN | R: RANGES | W: WAYPOINTS | D: DESIG ]",
             10, 10, 10, {120, 120, 130, 160});
}

// --- Main draw entry point ---

void viewer_draw(const ViewerState& vs) {
    ClearBackground({8, 8, 10, 255});
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
    draw_messages(vs);
    draw_ui(vs);
}
