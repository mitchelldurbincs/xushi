#pragma once

#include "../src/json.h"
#include "../src/scenario.h"
#include <string>
#include <vector>
#include <map>

struct TickFrame {
    std::vector<JsonValue> detections;
    std::vector<JsonValue> track_updates;
    std::vector<JsonValue> track_expired;
    std::vector<JsonValue> messages; // sent, delivered, dropped
    std::string world_hash;
    JsonValue stats_snapshot; // optional; NUL when unavailable for this tick
};

struct ViewerState {
    // Scenario data
    Scenario scenario;
    std::string scenario_path;

    // Indexed replay data
    std::vector<TickFrame> frames; // one per tick
    int total_ticks = 0;
    std::map<EntityId, const ScenarioEntity*> entities_by_id;

    // Playback
    int current_tick = 0;
    bool playing = false;
    float playback_speed = 5.0f; // ticks per second
    float tick_accumulator = 0.0f;

    // Camera
    float cam_x = 0.0f;
    float cam_y = 0.0f;
    float zoom = 1.0f;

    // UI
    bool dragging_slider = false;
};

struct WorldBounds {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool has_points = false;
};

WorldBounds compute_world_bounds(const Scenario& scenario);

void viewer_load(ViewerState& vs, const std::string& replay_path);
void viewer_update(ViewerState& vs);
void viewer_draw(const ViewerState& vs);
