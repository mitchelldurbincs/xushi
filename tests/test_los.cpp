#include "../src/map.h"
#include <cstdio>
#include <cstdlib>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(expr, name) do { \
    tests_run++; \
    if (expr) { \
        tests_passed++; \
        std::printf("  PASS  %s\n", name); \
    } else { \
        std::printf("  FAIL  %s\n", name); \
    } \
} while(0)

static void test_clear_los_no_obstacles() {
    Map map;
    CHECK(map.line_of_sight({0, 0}, {10, 10}), "clear LOS, no obstacles");
}

static void test_blocked_by_obstacle() {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    CHECK(!map.line_of_sight({0, 5}, {10, 5}), "blocked by centered obstacle");
}

static void test_not_blocked_when_obstacle_is_off_path() {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    CHECK(map.line_of_sight({0, 0}, {10, 0}), "obstacle above path, not blocking");
}

static void test_multiple_obstacles_one_blocks() {
    Map map;
    map.obstacles.push_back({{1, 1}, {2, 2}});   // not in the way
    map.obstacles.push_back({{4, 4}, {6, 6}});   // blocks
    CHECK(!map.line_of_sight({0, 5}, {10, 5}), "second obstacle blocks");
}

static void test_from_inside_obstacle() {
    Map map;
    map.obstacles.push_back({{0, 0}, {10, 10}});
    CHECK(!map.line_of_sight({5, 5}, {20, 5}), "from inside obstacle is blocked");
}

static void test_to_inside_obstacle() {
    Map map;
    map.obstacles.push_back({{8, 3}, {12, 7}});
    CHECK(!map.line_of_sight({0, 5}, {10, 5}), "to inside obstacle is blocked");
}

static void test_zero_length_segment_clear() {
    Map map;
    CHECK(map.line_of_sight({5, 5}, {5, 5}), "zero-length segment, no obstacles");
}

static void test_parallel_to_obstacle_edge() {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    // Segment runs along y=4 (the bottom edge of the obstacle)
    // Touching the boundary counts as intersection with our slab method
    CHECK(!map.line_of_sight({0, 4}, {10, 4}), "segment along obstacle edge is blocked");
}

static void test_transition_blocked_to_clear() {
    Map map;
    // Building from x=4..6, y=4..6
    map.obstacles.push_back({{4, 4}, {6, 6}});

    Vec2 observer = {0, 5};

    // Target behind obstacle
    CHECK(!map.line_of_sight(observer, {5, 5}), "target behind obstacle");
    // Target past obstacle
    CHECK(map.line_of_sight(observer, {10, 0}), "target past obstacle, clear");
}

int main() {
    std::printf("Running LOS tests...\n");

    test_clear_los_no_obstacles();
    test_blocked_by_obstacle();
    test_not_blocked_when_obstacle_is_off_path();
    test_multiple_obstacles_one_blocks();
    test_from_inside_obstacle();
    test_to_inside_obstacle();
    test_zero_length_segment_clear();
    test_parallel_to_obstacle_edge();
    test_transition_blocked_to_clear();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
