#include "test_helpers.h"
#include "../src/sensing.h"
#include <cmath>

static void test_blocked_los_no_detection() {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    Rng rng(1);
    Observation obs{};
    CHECK(!sense(map, {0, 5}, 0, {10, 5}, 1, 100.0f, 0, rng, obs), "LOS blocked -> no detection");
}

static void test_out_of_range_no_detection() {
    Map map;
    Rng rng(1);
    Observation obs{};
    CHECK(!sense(map, {0, 0}, 0, {200, 0}, 1, 50.0f, 0, rng, obs), "out of range -> no detection");
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
    Map map;
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
    Rng rng_a(77), rng_b(77);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    CHECK(a.estimated_position.x == b.estimated_position.x &&
          a.estimated_position.y == b.estimated_position.y, "same seed -> same observation");
}

static void test_different_seeds_differ() {
    Map map;
    Rng rng_a(77), rng_b(999);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    CHECK(a.estimated_position.x != b.estimated_position.x ||
          a.estimated_position.y != b.estimated_position.y, "different seeds -> different observations");
}

static void test_class_id_carried_through() {
    Map map;
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                          /*miss_rate=*/0.0f, /*target_class_id=*/5, /*class_confusion_rate=*/0.0f);
    CHECK(detected, "detected");
    CHECK(obs.class_id == 5, "class_id matches truth when no confusion");
    CHECK(obs.identity_confidence > 0.0f, "identity_confidence is positive");
}

static void test_class_confusion_always_confuses_at_rate_1() {
    Map map;
    int confused_count = 0;
    for (int i = 0; i < 100; ++i) {
        Rng rng(42 + i);
        Observation obs{};
        bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                              0.0f, /*target_class_id=*/3, /*class_confusion_rate=*/1.0f);
        if (detected && obs.class_id != 3) confused_count++;
    }
    CHECK(confused_count == 100, "100% confusion rate always produces wrong class");
}

static void test_class_confusion_never_confuses_at_rate_0() {
    Map map;
    for (int i = 0; i < 100; ++i) {
        Rng rng(42 + i);
        Observation obs{};
        bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                              0.0f, /*target_class_id=*/3, /*class_confusion_rate=*/0.0f);
        CHECK(detected, "should detect");
        CHECK(obs.class_id == 3, "0% confusion rate preserves class");
    }
}

int main() {
    std::printf("Running sensing tests...\n");
    test_blocked_los_no_detection();
    test_out_of_range_no_detection();
    test_clear_los_in_range_detects();
    test_noise_within_bounds();
    test_determinism_same_seed();
    test_different_seeds_differ();
    test_class_id_carried_through();
    test_class_confusion_always_confuses_at_rate_1();
    test_class_confusion_never_confuses_at_rate_0();
    TEST_REPORT();
}
