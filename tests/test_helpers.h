#pragma once

#include <cstdio>

struct TestContext {
    int tests_run = 0;
    int tests_passed = 0;

    void check(bool expr, const char* name) {
        tests_run++;
        if (expr) {
            tests_passed++;
            std::printf("  PASS  %s\n", name);
        } else {
            std::printf("  FAIL  %s\n", name);
        }
    }

    int report_and_exit_code() const {
        std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
        return (tests_passed == tests_run) ? 0 : 1;
    }
};

template <typename RegisterTestsFn>
int run_test_suite(const char* suite_name, RegisterTestsFn register_tests) {
    std::printf("Running %s tests...\n", suite_name);
    TestContext ctx;
    register_tests(ctx);
    return ctx.report_and_exit_code();
}

