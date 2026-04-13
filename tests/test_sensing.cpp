#include "test_helpers.h"
#include "../src/sensing.h"
#include <cmath>

static void test_blocked_los_no_detection(TestContext& ctx) {
    Map map;
    map.obstacles.push_back({{4, 4}, {6, 6}});
    Rng rng(1);
    Observation obs{};
    ctx.check(!sense(map, {0, 5}, 0, {10, 5}, 1, 100.0f, 0, rng, obs), "LOS blocked -> no detection");
}

static void test_out_of_range_no_detection(TestContext& ctx) {
    Map map;
    Rng rng(1);
    Observation obs{};
    ctx.check(!sense(map, {0, 0}, 0, {200, 0}, 1, 50.0f, 0, rng, obs), "out of range -> no detection");
}

static void test_clear_los_in_range_detects(TestContext& ctx) {
    Map map;
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 5, rng, obs);
    ctx.check(detected, "clear LOS in range -> detection");
    ctx.check(obs.tick == 5, "observation tick matches");
    ctx.check(obs.observer == 0, "observation observer matches");
    ctx.check(obs.target == 1, "observation target matches");
    ctx.check(obs.confidence > 0.0f && obs.confidence <= 1.0f, "confidence in (0, 1]");
    ctx.check(obs.uncertainty >= 0.0f, "uncertainty non-negative");
}

static void test_noise_within_bounds(TestContext& ctx) {
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
    ctx.check(max_error < 5.0f, "noise stays within reasonable bounds over 1000 trials");
}

static void test_determinism_same_seed(TestContext& ctx) {
    Map map;
    Rng rng_a(77), rng_b(77);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    ctx.check(a.estimated_position.x == b.estimated_position.x &&
          a.estimated_position.y == b.estimated_position.y, "same seed -> same observation");
}

static void test_different_seeds_differ(TestContext& ctx) {
    Map map;
    Rng rng_a(77), rng_b(999);
    Observation a{}, b{};
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_a, a);
    sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng_b, b);
    ctx.check(a.estimated_position.x != b.estimated_position.x ||
          a.estimated_position.y != b.estimated_position.y, "different seeds -> different observations");
}

static void test_class_id_carried_through(TestContext& ctx) {
    Map map;
    Rng rng(1);
    Observation obs{};
    bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                          /*miss_rate=*/0.0f, /*target_class_id=*/5, /*class_confusion_rate=*/0.0f);
    ctx.check(detected, "detected");
    ctx.check(obs.class_id == 5, "class_id matches truth when no confusion");
    ctx.check(obs.identity_confidence > 0.0f, "identity_confidence is positive");
}

static void test_class_confusion_always_confuses_at_rate_1(TestContext& ctx) {
    Map map;
    int confused_count = 0;
    for (int i = 0; i < 100; ++i) {
        Rng rng(42 + i);
        Observation obs{};
        bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                              0.0f, /*target_class_id=*/3, /*class_confusion_rate=*/1.0f);
        if (detected && obs.class_id != 3) confused_count++;
    }
    ctx.check(confused_count == 100, "100% confusion rate always produces wrong class");
}

static void test_class_confusion_never_confuses_at_rate_0(TestContext& ctx) {
    Map map;
    for (int i = 0; i < 100; ++i) {
        Rng rng(42 + i);
        Observation obs{};
        bool detected = sense(map, {0, 0}, 0, {10, 0}, 1, 50.0f, 0, rng, obs,
                              0.0f, /*target_class_id=*/3, /*class_confusion_rate=*/0.0f);
        ctx.check(detected, "should detect");
        ctx.check(obs.class_id == 3, "0% confusion rate preserves class");
    }
}

int main() {
    TestContext ctx;
    std::printf("Running sensing tests...\n");
    test_blocked_los_no_detection(ctx);
    test_out_of_range_no_detection(ctx);
    test_clear_los_in_range_detects(ctx);
    test_noise_within_bounds(ctx);
    test_determinism_same_seed(ctx);
    test_different_seeds_differ(ctx);
    test_class_id_carried_through(ctx);
    test_class_confusion_always_confuses_at_rate_1(ctx);
    test_class_confusion_never_confuses_at_rate_0(ctx);
    return ctx.report_and_exit_code();
}
