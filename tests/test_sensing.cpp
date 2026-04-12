#include "../src/sensing.h"
#include <cstdio>
#include <cmath>

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

static void test_blocked_los_no_detection() {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 5}, 0, {10, 5}, 1, 100.0f, 0, rng, obs);
    CHECK(!detected, "LOS blocked -> no detection");
}

static void test_out_of_range_no_detection() {
    Map map;
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {200, 0}, 1, 50.0f, 0, rng, obs);
    CHECK(!detected, "out of range -> no detection");
}

static void test_clear_los_in_range_detects() {
    Map map;
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 5, rng, obs);
    CHECK(detected, "clear LOS in range -> detection");
    CHECK(obs.tick == 5, "observation tick matches");
    CHECK(obs.observer == 0, "observation observer matches");
    CHECK(obs.target == 1, "observation target matches");
    CHECK(obs.confidence > 0.0f && obs.confidence <= 1.0f, "confidence in (0, 1]");
    CHECK(obs.uncertainty >= 0.0f, "uncertainty non-negative");
}

static void test_noise_within_bounds() {
    // At range 10 with max_range 50, range_frac = 0.2, noise_stddev = 0.4m.
    // Over many trials, error should stay within ~4 stddev (1.6m) almost always.
    Map map;
    Rng rng(42);
    float max_error = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        Observation obs{};
        Rng trial_rng(42 + i);
        sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, i, trial_rng, obs);
        Vec2 err = obs.estimated_position - Vec2{10, 0};
        float e = err.length();
        if (e > max_error) max_error = e;
    }
    CHECK(max_error < 5.0f, "noise stays within reasonable bounds over 1000 trials");
}

static void test_determinism_same_seed() {
    Map map;
    Rng rng_a(77);
    Rng rng_b(77);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    CHECK(a.estimated_position.x == b.estimated_position.x &&
          a.estimated_position.y == b.estimated_position.y,
          "same seed -> same observation");
}

static void test_different_seeds_differ() {
    Map map;
    Rng rng_a(77);
    Rng rng_b(999);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    CHECK(a.estimated_position.x != b.estimated_position.x ||
          a.estimated_position.y != b.estimated_position.y,
          "different seeds -> different observations");
}

int main() {
    std::printf("Running sensing tests...\n");

    test_blocked_los_no_detection();
    test_out_of_range_no_detection();
    test_clear_los_in_range_detects();
    test_noise_within_bounds();
    test_determinism_same_seed();
    test_different_seeds_differ();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
