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

// --- Entities ---

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

        DrawCircleV(pos, r, fill);
        DrawCircleLinesV(pos, r, outline);

        // Label
        const char* label = TextFormat("%s %u", ent.role_name.c_str(), ent.id);
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

// --- Main draw entry point ---

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
