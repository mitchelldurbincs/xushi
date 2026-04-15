#include "test_helpers.h"
#include "../src/comm.h"

static void test_jam_basic(TestContext& ctx) {
    CommSystem c;
    ctx.check(!c.is_jammed({5, 5}), "no jams, no cell is jammed");
    c.add_jam({5, 5}, 3, 1, /*issued_by*/ 0);
    ctx.check(c.is_jammed({5, 5}), "center of jam is jammed");
    ctx.check(c.is_jammed({7, 5}), "within chebyshev radius jammed");
    ctx.check(c.is_jammed({8, 8}), "diagonal within radius jammed");
    ctx.check(!c.is_jammed({9, 5}), "outside radius not jammed");
}

static void test_jam_tick_down(TestContext& ctx) {
    CommSystem c;
    c.add_jam({0, 0}, 2, 2, /*issued_by*/ 1);
    c.tick_down();
    ctx.check(c.is_jammed({1, 1}), "still active after 1 tick");
    c.tick_down();
    ctx.check(!c.is_jammed({1, 1}), "expired after duration");
}

static void test_multiple_jams(TestContext& ctx) {
    CommSystem c;
    c.add_jam({0, 0}, 1, 1, 0);
    c.add_jam({10, 10}, 1, 1, 1);
    ctx.check(c.is_jammed({0, 0}) && c.is_jammed({10, 10}),
              "both jams cover their centers");
    ctx.check(!c.is_jammed({5, 5}), "between jams is clear");
}

int main() {
    return run_test_suite("comm", [](TestContext& ctx) {
        test_jam_basic(ctx);
        test_jam_tick_down(ctx);
        test_multiple_jams(ctx);
    });
}
