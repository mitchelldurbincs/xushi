#include "test_helpers.h"
#include "../src/map.h"

static void test_clear_los_no_obstacles(TestContext& ctx) {
    Map map;
    ctx.check(map.line_of_sight({0, 0}, {10, 10}), "clear LOS, no obstacles");
}

static void test_blocked_by_obstacle(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    ctx.check(!map.line_of_sight({0, 5}, {10, 5}), "blocked by centered obstacle");
}

static void test_not_blocked_when_obstacle_is_off_path(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    ctx.check(map.line_of_sight({0, 0}, {10, 0}), "obstacle above path, not blocking");
}

static void test_multiple_obstacles_one_blocks(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{1, 1}, {2, 2}});
    map.obstacles.push_back({{4, 4}, {6, 6}});
    ctx.check(!map.line_of_sight({0, 5}, {10, 5}), "second obstacle blocks");
}

static void test_from_inside_obstacle(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{0, 0}, {10, 10}});
    ctx.check(!map.line_of_sight({5, 5}, {20, 5}), "from inside obstacle is blocked");
}

static void test_to_inside_obstacle(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{8, 3}, {12, 7}});
    ctx.check(!map.line_of_sight({0, 5}, {10, 5}), "to inside obstacle is blocked");
}

static void test_zero_length_segment_clear(TestContext& ctx) {
    Map map;
    ctx.check(map.line_of_sight({5, 5}, {5, 5}), "zero-length segment, no obstacles");
}

static void test_parallel_to_obstacle_edge(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    ctx.check(!map.line_of_sight({0, 4}, {10, 4}), "segment along obstacle edge is blocked");
}

static void test_transition_blocked_to_clear(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    Vec2 observer = {0, 5};
    ctx.check(!map.line_of_sight(observer, {5, 5}), "target behind obstacle");
    ctx.check(map.line_of_sight(observer, {10, 0}), "target past obstacle, clear");
}

int main() {
    TestContext ctx;
    std::printf("Running LOS tests...\n");
    test_clear_los_no_obstacles(ctx);
    test_blocked_by_obstacle(ctx);
    test_not_blocked_when_obstacle_is_off_path(ctx);
    test_multiple_obstacles_one_blocks(ctx);
    test_from_inside_obstacle(ctx);
    test_to_inside_obstacle(ctx);
    test_zero_length_segment_clear(ctx);
    test_parallel_to_obstacle_edge(ctx);
    test_transition_blocked_to_clear(ctx);
    return ctx.report_and_exit_code();
}
