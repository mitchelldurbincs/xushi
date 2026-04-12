#include "test_helpers.h"
#include "../src/rng.h"
#include <cmath>

static void test_same_seed_same_sequence() {
    Rng a(42), b(42);
    bool same = true;
    for (int i = 0; i < 100; ++i)
        if (a.next() != b.next()) { same = false; break; }
    CHECK(same, "same seed produces same sequence");
}

static void test_different_seeds_differ() {
    Rng a(42), b(99);
    bool any_diff = false;
    for (int i = 0; i < 10; ++i)
        if (a.next() != b.next()) { any_diff = true; break; }
    CHECK(any_diff, "different seeds produce different sequences");
}

static void test_uniform_in_range() {
    Rng rng(123);
    bool in_range = true;
    for (int i = 0; i < 10000; ++i) {
        float v = rng.uniform();
        if (v < 0.0f || v >= 1.0f) { in_range = false; break; }
    }
    CHECK(in_range, "uniform() output in [0, 1)");
}

static void test_normal_mean_near_zero() {
    Rng rng(456);
    double sum = 0.0;
    int n = 10000;
    for (int i = 0; i < n; ++i) sum += rng.normal();
    double mean = sum / n;
    CHECK(std::fabs(mean) < 0.1, "normal() mean near zero over 10k samples");
}

int main() {
    std::printf("Running RNG tests...\n");
    test_same_seed_same_sequence();
    test_different_seeds_differ();
    test_uniform_in_range();
    test_normal_mean_near_zero();
    TEST_REPORT();
}
