#include "viewer.h"
#include "raylib.h"
#include <algorithm>

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

    // Overlay toggles
    if (IsKeyPressed(KEY_R))
        vs.show_sensor_ranges = !vs.show_sensor_ranges;
    if (IsKeyPressed(KEY_W))
        vs.show_waypoint_paths = !vs.show_waypoint_paths;

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
