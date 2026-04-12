#include "viewer.h"
#include "raylib.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: xushi_viewer <replay_file>\n");
        return 1;
    }

    const char* replay_path = argv[1];

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1200, 800, "xushi replay viewer");
    SetTargetFPS(60);

    ViewerState vs;
    try {
        viewer_load(vs, replay_path);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "error loading replay: %s\n", e.what());
        CloseWindow();
        return 1;
    }

    std::printf("loaded replay: %d ticks, %zu entities, %zu obstacles\n",
                vs.total_ticks,
                vs.scenario.entities.size(),
                vs.scenario.obstacles.size());

    while (!WindowShouldClose()) {
        viewer_update(vs);

        BeginDrawing();
        viewer_draw(vs);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
