#include "test_helpers.h"
#include "../src/patrol_policy.h"
#include <cmath>

static void test_null_policy_returns_nullopt() {
    NullPolicy np;
    PolicyObservation obs;
    obs.id = 0;
    obs.position = {10, 20};
    obs.tick = 0;
    auto result = np.get_move_target(obs);
    CHECK(!result.has_value(), "NullPolicy returns nullopt");
}

static void test_patrol_returns_first_waypoint() {
    PatrolPolicy pp;
    pp.routes[0] = {{{10, 0}, {20, 0}, {20, 10}}, 0};

    PolicyObservation obs;
    obs.id = 0;
    obs.position = {0, 0};
    obs.tick = 0;

    auto target = pp.get_move_target(obs);
    CHECK(target.has_value(), "patrol returns a target");
    CHECK(std::fabs(target->x - 10.0f) < 0.001f, "patrol first target x");
    CHECK(std::fabs(target->y - 0.0f) < 0.001f, "patrol first target y");
}

static void test_patrol_advances_on_arrival() {
    PatrolPolicy pp;
    pp.routes[0] = {{{10, 0}, {20, 0}, {20, 10}}, 0};

    PolicyObservation obs;
    obs.id = 0;
    obs.tick = 0;

    // Agent is at waypoint 0 (within arrival radius)
    obs.position = {10, 0};
    auto target = pp.get_move_target(obs);
    CHECK(target.has_value(), "patrol advances: has target");
    CHECK(std::fabs(target->x - 20.0f) < 0.001f, "patrol advances to wp1 x");
    CHECK(std::fabs(target->y - 0.0f) < 0.001f, "patrol advances to wp1 y");
}

static void test_patrol_loops() {
    PatrolPolicy pp;
    pp.routes[0] = {{{10, 0}, {20, 0}}, 0};

    PolicyObservation obs;
    obs.id = 0;
    obs.tick = 0;

    // Arrive at wp0 -> advance to wp1
    obs.position = {10, 0};
    pp.get_move_target(obs);

    // Arrive at wp1 -> should loop to wp0
    obs.position = {20, 0};
    auto target = pp.get_move_target(obs);
    CHECK(target.has_value(), "patrol loops: has target");
    CHECK(std::fabs(target->x - 10.0f) < 0.001f, "patrol loops back to wp0 x");
}

static void test_patrol_unknown_entity_returns_nullopt() {
    PatrolPolicy pp;
    pp.routes[0] = {{{10, 0}}, 0};

    PolicyObservation obs;
    obs.id = 99;  // not in routes
    obs.position = {0, 0};
    obs.tick = 0;

    auto target = pp.get_move_target(obs);
    CHECK(!target.has_value(), "patrol returns nullopt for unknown entity");
}

int main() {
    std::printf("Running policy tests...\n");
    test_null_policy_returns_nullopt();
    test_patrol_returns_first_waypoint();
    test_patrol_advances_on_arrival();
    test_patrol_loops();
    test_patrol_unknown_entity_returns_nullopt();
    TEST_REPORT();
}
