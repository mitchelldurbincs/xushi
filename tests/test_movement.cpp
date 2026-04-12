#include "test_helpers.h"
#include "../src/movement.h"
#include "../src/rng.h"
#include <cmath>

static Rng test_rng(12345);

static void test_constant_velocity_unchanged() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.velocity = {3.0f, 4.0f};
    // No waypoints

    auto event = update_movement(e, 1.0f, test_rng);
    CHECK(!event.arrived, "no arrival for constant velocity");
    CHECK(std::fabs(e.position.x - 3.0f) < 0.001f, "constant vel x after 1 tick");
    CHECK(std::fabs(e.position.y - 4.0f) < 0.001f, "constant vel y after 1 tick");

    update_movement(e, 1.0f, test_rng);
    CHECK(std::fabs(e.position.x - 6.0f) < 0.001f, "constant vel x after 2 ticks");
    CHECK(std::fabs(e.position.y - 8.0f) < 0.001f, "constant vel y after 2 ticks");
}

static void test_waypoint_approach() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.velocity = {0.0f, 0.0f};
    e.waypoints = {{10.0f, 0.0f}};
    e.speed = 2.0f;

    update_movement(e, 1.0f, test_rng);
    CHECK(std::fabs(e.position.x - 2.0f) < 0.001f, "waypoint approach: x after 1 tick");
    CHECK(std::fabs(e.position.y) < 0.001f, "waypoint approach: y stays 0");

    update_movement(e, 1.0f, test_rng);
    CHECK(std::fabs(e.position.x - 4.0f) < 0.001f, "waypoint approach: x after 2 ticks");
}

static void test_waypoint_advance() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.waypoints = {{5.0f, 0.0f}, {5.0f, 10.0f}};
    e.speed = 5.0f;

    CHECK(e.current_waypoint == 0, "starts at waypoint 0");

    // After 1 tick at speed 5, entity reaches (5,0) — should arrive and advance
    auto event = update_movement(e, 1.0f, test_rng);
    CHECK(event.arrived, "arrived at first waypoint");
    CHECK(event.waypoint_index == 0, "arrived at index 0");
    CHECK(e.current_waypoint == 1, "advanced to waypoint 1");
}

static void test_stop_mode() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.waypoints = {{3.0f, 0.0f}};
    e.speed = 5.0f;
    e.waypoint_mode = ScenarioEntity::WaypointMode::Stop;

    // Overshoot snap to (3,0), then stop
    auto event = update_movement(e, 1.0f, test_rng);
    CHECK(event.arrived, "arrived at only waypoint");
    CHECK(std::fabs(e.position.x - 3.0f) < 0.001f, "snapped to waypoint");

    // Should not move further
    Vec2 before = e.position;
    update_movement(e, 1.0f, test_rng);
    CHECK(std::fabs(e.position.x - before.x) < 0.001f, "stopped after last waypoint x");
    CHECK(std::fabs(e.position.y - before.y) < 0.001f, "stopped after last waypoint y");
}

static void test_loop_mode() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.waypoints = {{4.0f, 0.0f}, {4.0f, 4.0f}};
    e.speed = 5.0f;
    e.waypoint_mode = ScenarioEntity::WaypointMode::Loop;

    // Tick 1: reach wp 0 (4,0), advance to wp 1
    update_movement(e, 1.0f, test_rng);
    CHECK(e.current_waypoint == 1, "loop: advanced to wp 1");

    // Tick 2: reach wp 1 (4,4), advance wraps to wp 0
    update_movement(e, 1.0f, test_rng);
    CHECK(e.current_waypoint == 0, "loop: wrapped to wp 0");

    // Should continue moving (not stopped)
    Vec2 before = e.position;
    update_movement(e, 1.0f, test_rng);
    float dx = e.position.x - before.x;
    float dy = e.position.y - before.y;
    CHECK(dx * dx + dy * dy > 0.01f, "loop: still moving after wrap");
}

static void test_overshoot_snap() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.waypoints = {{3.0f, 0.0f}};
    e.speed = 5.0f;  // would move 5 units, but waypoint is only 3 away

    update_movement(e, 1.0f, test_rng);
    CHECK(std::fabs(e.position.x - 3.0f) < 0.001f, "overshoot: snapped to waypoint x");
    CHECK(std::fabs(e.position.y) < 0.001f, "overshoot: y unchanged");
}

static void test_arrival_event() {
    ScenarioEntity e;
    e.id = 0;
    e.position = {0.0f, 0.0f};
    e.waypoints = {{10.0f, 0.0f}};
    e.speed = 2.0f;

    // Should not arrive on first few ticks
    auto ev1 = update_movement(e, 1.0f, test_rng);
    CHECK(!ev1.arrived, "not arrived after 1 tick");

    auto ev2 = update_movement(e, 1.0f, test_rng);
    CHECK(!ev2.arrived, "not arrived after 2 ticks");

    // Move to within arrival radius
    e.position = {9.5f, 0.0f};
    auto ev3 = update_movement(e, 1.0f, test_rng);
    CHECK(ev3.arrived, "arrived when within radius");
    CHECK(ev3.waypoint_index == 0, "arrival reports correct index");
}

int main() {
    std::printf("Running movement tests...\n");
    test_constant_velocity_unchanged();
    test_waypoint_approach();
    test_waypoint_advance();
    test_stop_mode();
    test_loop_mode();
    test_overshoot_snap();
    test_arrival_event();
    TEST_REPORT();
}
