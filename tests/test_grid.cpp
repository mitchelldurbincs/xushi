#include "test_helpers.h"
#include "../src/grid.h"

static void test_bresenham_los_basic(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWW",
        "W...W",
        "W...W",
        "W...W",
        "WWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);
    g.recompute_rooms();

    ctx.check(g.line_of_sight({1, 1}, {3, 3}), "LOS open across empty room");
    ctx.check(g.line_of_sight({1, 1}, {1, 1}), "LOS to self is true");
}

static void test_los_blocked_by_wall(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWWWW",
        "W.....W",
        "W.WWW.W",
        "W.....W",
        "WWWWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);

    // Through the wall row: (2,1) -> (2,3) should be blocked by wall at (2,2)
    ctx.check(!g.line_of_sight({2, 1}, {2, 3}), "wall blocks vertical LOS");
    // Around the wall
    ctx.check(g.line_of_sight({1, 1}, {1, 3}), "LOS passes through side of wall");
}

static void test_cover_does_not_block_los(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWWWW",
        "W.....W",
        "W..C..W",
        "W.....W",
        "WWWWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);

    // COVER at (3,2); LOS from (1,2) to (5,2) should pass through COVER.
    ctx.check(g.line_of_sight({1, 2}, {5, 2}), "COVER does not block LOS");
}

static void test_los_symmetry(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWWWW",
        "W.....W",
        "W.W...W",
        "W.....W",
        "WWWWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);
    bool all_symmetric = true;
    for (int y1 = 0; y1 < g.height(); ++y1) {
        for (int x1 = 0; x1 < g.width(); ++x1) {
            GridPos a{static_cast<int16_t>(x1), static_cast<int16_t>(y1)};
            if (g.blocks_los(a)) continue;
            for (int y2 = 0; y2 < g.height(); ++y2) {
                for (int x2 = 0; x2 < g.width(); ++x2) {
                    GridPos b{static_cast<int16_t>(x2), static_cast<int16_t>(y2)};
                    if (g.blocks_los(b)) continue;
                    if (g.line_of_sight(a, b) != g.line_of_sight(b, a)) {
                        all_symmetric = false;
                    }
                }
            }
        }
    }
    ctx.check(all_symmetric, "LOS is symmetric for all floor cell pairs");
}

static void test_closed_door_blocks_los(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWW",
        "W...W",
        "W...W",
        "W...W",
        "WWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);
    // Add a CLOSED door between (2,1) and (2,2).
    g.add_door({2, 1}, {2, 2}, DoorState::CLOSED);
    ctx.check(!g.line_of_sight({2, 1}, {2, 3}),
              "CLOSED door blocks LOS across its edge");
    ctx.check(!g.edge_passable({2, 1}, {2, 2}),
              "CLOSED door is not passable");

    // Open the door.
    g.doors()[0].state = DoorState::OPEN;
    ctx.check(g.line_of_sight({2, 1}, {2, 3}),
              "OPEN door allows LOS");
    ctx.check(g.edge_passable({2, 1}, {2, 2}),
              "OPEN door is passable");
}

static void test_rooms_separated_by_doors(TestContext& ctx) {
    // Two rooms connected by a door at (3,2)<->(3,3). When closed, two
    // rooms; when open, one. Row 3 has a single FLOOR cell at x=3 to bridge
    // the two room halves; the door sits on the edge above it.
    std::vector<std::string> rows = {
        "WWWWWWW",
        "W.....W",
        "W.....W",
        "WWW.WWW",
        "W.....W",
        "W.....W",
        "WWWWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);
    // Place a CLOSED door between the gap cells (3,2) and (3,3).
    g.add_door({3, 2}, {3, 3}, DoorState::CLOSED);
    g.recompute_rooms();
    int top_room = g.room_of({1, 1});
    int bottom_room = g.room_of({1, 5});
    ctx.check(top_room >= 0 && bottom_room >= 0,
              "both cells have valid room ids");
    ctx.check(top_room != bottom_room,
              "CLOSED door separates rooms");

    g.doors()[0].state = DoorState::OPEN;
    g.recompute_rooms();
    ctx.check(g.room_of({1, 1}) == g.room_of({1, 5}),
              "OPEN door unifies rooms");
}

static void test_neighbors_8_honors_doors_and_walls(TestContext& ctx) {
    std::vector<std::string> rows = {
        "WWWWW",
        "W...W",
        "W.W.W",
        "W...W",
        "WWWWW",
    };
    GridMap g = GridMap::from_ascii(rows);
    auto n = g.neighbors_8({2, 1});
    // (2,1) neighbors in 8: (1,0)W (2,0)W (3,0)W (1,1). (3,1). (1,2). (2,2)W (3,2).
    // Walls are filtered out; the interior wall at (2,2) is excluded.
    // Expected: (1,1),(3,1),(1,2),(3,2) = 4 neighbors.
    ctx.check(n.size() == 4, "neighbors_8 excludes WALL cells");
}

static void test_bounds(TestContext& ctx) {
    std::vector<std::string> rows = {"W.W", ".W.", "W.W"};
    GridMap g = GridMap::from_ascii(rows);
    ctx.check(g.width() == 3 && g.height() == 3, "dimensions parsed");
    ctx.check(g.in_bounds({0, 0}), "origin in bounds");
    ctx.check(g.in_bounds({2, 2}), "max in bounds");
    ctx.check(!g.in_bounds({3, 0}), "out-of-bounds x rejected");
    ctx.check(!g.in_bounds({0, -1}), "out-of-bounds negative rejected");
}

int main() {
    return run_test_suite("grid", [](TestContext& ctx) {
        test_bresenham_los_basic(ctx);
        test_los_blocked_by_wall(ctx);
        test_cover_does_not_block_los(ctx);
        test_los_symmetry(ctx);
        test_closed_door_blocks_los(ctx);
        test_rooms_separated_by_doors(ctx);
        test_neighbors_8_honors_doors_and_walls(ctx);
        test_bounds(ctx);
    });
}
